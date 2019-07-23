// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Contains the implementation of class Crypto

#include "cryptohome/crypto.h"

#include <sys/types.h>

#include <crypto/sha2.h>
#include <limits>
#include <map>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <unistd.h>
#include <utility>
#include <malloc.h>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>
extern "C" {
#include <scrypt/crypto_scrypt.h>
#include <scrypt/scryptenc.h>
}

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm_init.h"
#include "cryptohome/vault_keyset.h"

#include "attestation.pb.h"  // NOLINT(build/include)

using base::FilePath;
using brillo::SecureBlob;

namespace cryptohome {

namespace {

// The maximum number of times to try decryption with the TPM.
constexpr int kTpmDecryptMaxRetries = 2;

// Helper function to aovid the double nested if statements involved with
// checking the error pointer. If |error| is |nullptr|, this does nothing.
template <typename ErrorType>
void PopulateError(ErrorType* error, ErrorType error_code) {
  if (error)
    *error = error_code;
}

bool TpmErrorIsRetriable(Tpm::TpmRetryAction retry_action) {
  return retry_action == Tpm::kTpmRetryLoadFail ||
         retry_action == Tpm::kTpmRetryInvalidHandle ||
         retry_action == Tpm::kTpmRetryCommFailure;
}

Crypto::CryptoError ConvertLeError(int le_error) {
  switch (le_error) {
    case LE_CRED_ERROR_INVALID_LE_SECRET:
      return Crypto::CE_LE_INVALID_SECRET;
    case LE_CRED_ERROR_TOO_MANY_ATTEMPTS:
      return Crypto::CE_TPM_DEFEND_LOCK;
    case LE_CRED_ERROR_INVALID_LABEL:
      return Crypto::CE_OTHER_FATAL;
    case LE_CRED_ERROR_HASH_TREE:
      return Crypto::CE_OTHER_FATAL;
    case LE_CRED_ERROR_PCR_NOT_MATCH:
      // We might want to return an error here that will make the device
      // reboot.
      LOG(ERROR) << "PCR in unexpected state.";
      return Crypto::CE_LE_INVALID_SECRET;
    default:
      return Crypto::CE_OTHER_FATAL;
  }
}

Crypto::CryptoError TpmErrorToCrypto(Tpm::TpmRetryAction retry_action) {
  switch (retry_action) {
    case Tpm::kTpmRetryFatal:
      // All errors mapped here will cause re-creating the cryptohome if
      // they occur when decrypting the keyset.
      return Crypto::CE_TPM_FATAL;
    case Tpm::kTpmRetryCommFailure:
    case Tpm::kTpmRetryInvalidHandle:
    case Tpm::kTpmRetryLoadFail:
    case Tpm::kTpmRetryLater:
      return Crypto::CE_TPM_COMM_ERROR;
    case Tpm::kTpmRetryDefendLock:
      return Crypto::CE_TPM_DEFEND_LOCK;
    case Tpm::kTpmRetryReboot:
      return Crypto::CE_TPM_REBOOT;
    default:
      // TODO(chromium:709646): kTpmRetryFailNoRetry maps here now. Find
      // a better corresponding CryptoError.
      return Crypto::CE_NONE;
  }
}

// Location where we store the Low Entropy (LE) credential manager related
// state.
const char kSignInHashTreeDir[] = "/home/.shadow/low_entropy_creds";

// An upper bound on the amount of memory that we allow Scrypt to use when
// performing key strengthening (32MB).  A large size is okay since we only use
// Scrypt during the login process, before the user is logged in.  This memory
// is managed (and freed) by the scrypt library.
const unsigned int kScryptMaxMem = 32 * 1024 * 1024;

// An upper bound on the amount of time we allow Scrypt to use when performing
// key strenthening (1/3s) for encryption.
const double kScryptMaxEncryptTime = 0.333;

// An upper bound on the amount of time we allow Scrypt to use when performing
// key strenthening (100s) for decryption.  This number can be high because in
// practice, it doesn't mean much.  It simply needs to be large enough that we
// can guarantee that the key derived during encryption can always be derived at
// decryption time, so the typical time is usually close to 1/3s.  However,
// because sometimes other processes may interfere, we need it to be large
// enough to allow the same calculation to be made amidst other heavy use.
const double kScryptMaxDecryptTime = 100.0;

// Scrypt creates a header in the cipher text that we need to account for in
// buffer sizing.
const unsigned int kScryptHeaderLength = 128;

// The number of hash rounds we originally used when converting a password to a
// key.  This is used when converting older cryptohome vault keysets.
const unsigned int kDefaultLegacyPasswordRounds = 1;

// AES key size in bytes (256-bit).  This key size is used for all key creation,
// though we currently only use 128 bits for the eCryptfs File Encryption Key
// (FEK).  Larger than 128-bit has too great of a CPU overhead on unaccelerated
// architectures.
const unsigned int kDefaultAesKeySize = 32;

// The size in bytes of password blob to be generated by SCrypt. Should be the
// same size as the modulus of cryptohome key, since we need to be able to
// decrypt it.
const unsigned int kDefaultPassBlobSize = 256;

// Maximum size of the salt file.
const int64_t kSystemSaltMaxSize = (1 << 20);  // 1 MB

// File permissions of salt file (modulo umask).
const mode_t kSaltFilePermissions = 0644;

// String used as vector in HMAC operation to derive vkk_seed from High Entropy
// secret.
const char kHESecretHmacData[] = "vkk_seed";

// A default delay schedule to be used for LE Credentials.
// The format for a delay schedule entry is as follows:
//
// (number_of_incorrect_attempts, delay before_next_attempt)
//
// The default schedule is for the first 5 incorrect attempts to have no delay,
// and no further attempts allowed.
const struct {
  uint32_t attempts;
  uint32_t delay;
} kDefaultDelaySchedule[] = {
  { 5, UINT32_MAX }
};

typedef SecureBlob HashFunction(const brillo::SecureBlob& data);

struct HashDescription {
  SecureBlob GetPcrAfterExtendingUsername(const SecureBlob& user_name) {
    if (!size) {
      return SecureBlob();
    }
    SecureBlob starting_value(size, 0);
    return func(SecureBlob::Combine(starting_value, func(user_name)));
  }

  size_t size;
  HashFunction* func;
};

HashDescription GetHashDescription(Tpm* tpm) {
  switch (tpm->GetVersion()) {
    case Tpm::TPM_1_2:
      return { SHA_DIGEST_LENGTH, &CryptoLib::Sha1 };
    case Tpm::TPM_2_0:
      return { SHA256_DIGEST_LENGTH, &CryptoLib::Sha256 };
    default:
      return { 0, nullptr };
  }
}

}  // namespace

// File name of the system salt file.
const char kSystemSaltFile[] = "salt";

Crypto::Crypto(Platform* platform)
    : use_tpm_(false),
      tpm_(NULL),
      platform_(platform),
      tpm_init_(NULL),
      scrypt_max_encrypt_time_(kScryptMaxEncryptTime) {
}

Crypto::~Crypto() {
}

bool Crypto::Init(TpmInit* tpm_init) {
  if (use_tpm_) {
    CHECK(tpm_init) << "Crypto wanted to use TPM but was not provided a TPM";
    if (tpm_ == NULL) {
      tpm_ = tpm_init->get_tpm();
    }
    tpm_init_ = tpm_init;
    tpm_init_->SetupTpm(true);
    if (tpm_->GetLECredentialBackend() &&
        tpm_->GetLECredentialBackend()->IsSupported()) {
      le_manager_ = std::make_unique<LECredentialManager>(
          tpm_->GetLECredentialBackend(), base::FilePath(kSignInHashTreeDir));
    }
  }
  return true;
}

Crypto::CryptoError Crypto::EnsureTpm(bool reload_key) const {
  Crypto::CryptoError result = Crypto::CE_NONE;
  if (tpm_ && tpm_init_) {
    if (reload_key || !tpm_init_->HasCryptohomeKey()) {
      tpm_init_->SetupTpm(true);
    }
  }
  return result;
}

bool Crypto::DeriveSecretsSCrypt(
    const brillo::SecureBlob& passkey,
    const brillo::SecureBlob& salt,
    std::vector<brillo::SecureBlob*> gen_secrets) const {
  // Scrypt parameters for deriving key material from UserPasskey.
  // N = kUPScryptWorkFactor
  // r = kUPScryptBlockSize
  // p = kUPScryptParallelFactor
  const uint64_t kUPScryptWorkFactor = (1 << 14);
  const uint32_t kUPScryptBlockSize = 8;
  const uint32_t kUPScryptParallelFactor = 1;

  size_t generated_len = 0;
  for (auto& secret : gen_secrets) {
    generated_len += secret->size();
  }

  SecureBlob generated(generated_len);
  if (crypto_scrypt(passkey.data(), passkey.size(), salt.data(), salt.size(),
                    kUPScryptWorkFactor, kUPScryptBlockSize,
                    kUPScryptParallelFactor, generated.data(),
                    generated.size())) {
    LOG(ERROR) << "Failed to derive scrypt keys from passkey.";
    return false;
  }
  // Release unused heap space after crypto_scrypt.
  // See crbug.com/899065 for details.
  malloc_trim(0);

  uint8_t* data = generated.data();
  for (auto& value : gen_secrets) {
    value->assign(data, data + value->size());
    data += value->size();
  }

  return true;
}

bool Crypto::PasskeyToTokenAuthData(const brillo::SecureBlob& passkey,
                                    const FilePath& salt_file,
                                    SecureBlob* auth_data) const {
  // Use the scrypt algorithm to derive auth data from the passkey.
  const size_t kAuthDataSizeBytes = 32;
  // The following scrypt parameters are based on Colin Percival's interactive
  // login example in http://www.tarsnap.com/scrypt/scrypt-slides.pdf and
  // adjusted for ~100ms performance on slower models. The performance is
  // dependant on CPU and memory performance.
  const uint64_t kScryptParameterN = (1 << 11);
  const uint32_t kScryptParameterR = 8;
  const uint32_t kScryptParameterP = 1;
  const unsigned int kSaltLength = 32;
  SecureBlob salt;
  if (!GetOrCreateSalt(salt_file, kSaltLength, false, &salt)) {
    LOG(ERROR) << "Failed to get authorization data salt.";
    return false;
  }
  SecureBlob local_auth_data;
  local_auth_data.resize(kAuthDataSizeBytes);
  if (0 != crypto_scrypt(passkey.data(),
                         passkey.size(),
                         salt.data(),
                         salt.size(),
                         kScryptParameterN,
                         kScryptParameterR,
                         kScryptParameterP,
                         local_auth_data.data(),
                         kAuthDataSizeBytes)) {
    LOG(ERROR) << "Scrypt key derivation failed.";
    return false;
  }
  // Release unused heap space after crypto_scrypt.
  // See crbug.com/899065 for details.
  malloc_trim(0);

  auth_data->swap(local_auth_data);
  return true;
}

bool Crypto::GetOrCreateSalt(const FilePath& path,
                             size_t length,
                             bool force,
                             SecureBlob* salt) const {
  int64_t file_len = 0;
  if (platform_->FileExists(path)) {
    if (!platform_->GetFileSize(path, &file_len)) {
      LOG(ERROR) << "Can't get file len for " << path.value();
      return false;
    }
  }
  SecureBlob local_salt;
  if (force || file_len == 0 || file_len > kSystemSaltMaxSize) {
    LOG(ERROR) << "Creating new salt at " << path.value()
               << " (" << force << ", " << file_len << ")";
    // If this salt doesn't exist, automatically create it.
    local_salt.resize(length);
    CryptoLib::GetSecureRandom(local_salt.data(), local_salt.size());
    if (!platform_->WriteSecureBlobToFileAtomicDurable(
            path, local_salt, kSaltFilePermissions)) {
      LOG(ERROR) << "Could not write user salt";
      return false;
    }
  } else {
    local_salt.resize(file_len);
    if (!platform_->ReadFileToSecureBlob(path, &local_salt)) {
      LOG(ERROR) << "Could not read salt file of length " << file_len;
      return false;
    }
  }
  salt->swap(local_salt);
  return true;
}

void Crypto::PasswordToPasskey(const char* password,
                               const brillo::SecureBlob& salt,
                               SecureBlob* passkey) {
  CHECK(password);

  std::string ascii_salt = CryptoLib::SecureBlobToHex(salt);
  // Convert a raw password to a password hash
  SHA256_CTX sha_context;
  SecureBlob md_value(SHA256_DIGEST_LENGTH);

  SHA256_Init(&sha_context);
  SHA256_Update(&sha_context, ascii_salt.data(), ascii_salt.length());
  SHA256_Update(&sha_context, password, strlen(password));
  SHA256_Final(md_value.data(), &sha_context);

  md_value.resize(SHA256_DIGEST_LENGTH / 2);
  SecureBlob local_passkey(SHA256_DIGEST_LENGTH);
  CryptoLib::SecureBlobToHexToBuffer(md_value,
                                     local_passkey.data(),
                                     local_passkey.size());
  passkey->swap(local_passkey);
}

bool Crypto::IsTPMPubkeyHash(const std::string& hash,
                             CryptoError* error) const {
  SecureBlob pub_key_hash;
  Tpm::TpmRetryAction retry_action = tpm_->GetPublicKeyHash(
      tpm_init_->GetCryptohomeKey(),
      &pub_key_hash);
  if (retry_action == Tpm::kTpmRetryLoadFail ||
      retry_action == Tpm::kTpmRetryInvalidHandle) {
    if (!tpm_init_->ReloadCryptohomeKey()) {
      LOG(ERROR) << "Unable to reload key.";
      retry_action = Tpm::kTpmRetryFailNoRetry;
    } else {
      retry_action = tpm_->GetPublicKeyHash(tpm_init_->GetCryptohomeKey(),
                                            &pub_key_hash);
    }
  }
  if (retry_action != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Unable to get the cryptohome public key from the TPM.";
    ReportCryptohomeError(kCannotReadTpmPublicKey);
    PopulateError(error, TpmErrorToCrypto(retry_action));
    return false;
  }
  if ((hash.size() != pub_key_hash.size()) ||
      (brillo::SecureMemcmp(hash.data(),
                              pub_key_hash.data(),
                              pub_key_hash.size()))) {
    PopulateError(error, CE_TPM_FATAL);
    return false;
  }
  return true;
}

SecureBlob Crypto::GetTpmKeyFromSerialized(
    const SerializedVaultKeyset& serialized,
    bool is_pcr_extended) const {
  bool is_pcr_bound =
      serialized.flags() & SerializedVaultKeyset::PCR_BOUND;
  auto tpm_key_data =
      (is_pcr_bound && is_pcr_extended) ? serialized.extended_tpm_key() :
                                          serialized.tpm_key();
  return SecureBlob(tpm_key_data);
}

bool Crypto::DecryptTPM(const SerializedVaultKeyset& serialized,
                        const SecureBlob& vault_key,
                        bool is_pcr_extended,
                        CryptoError* error,
                        KeyBlobs* key_out_data) const {
  CHECK(tpm_);
  CHECK(serialized.flags() & SerializedVaultKeyset::TPM_WRAPPED);

  if (!serialized.has_tpm_key()) {
    LOG(ERROR) << "Decrypting with TPM, but no tpm key present";
    ReportCryptohomeError(kDecryptAttemptButTpmKeyMissing);
    PopulateError(error, CE_TPM_FATAL);
    return false;
  }

  // If the TPM is enabled but not owned, and the keyset is TPM wrapped, then
  // it means the TPM has been cleared since the last login, and is not
  // re-owned.  In this case, the SRK is cleared and we cannot recover the
  // keyset.
  if (tpm_->IsEnabled() && !tpm_->IsOwned()) {
    LOG(ERROR) << "Fatal error--the TPM is enabled but not owned, and this "
               << "keyset was wrapped by the TPM.  It is impossible to "
               << "recover this keyset.";
    ReportCryptohomeError(kDecryptAttemptButTpmNotOwned);
    PopulateError(error, CE_TPM_FATAL);
    return false;
  }

  Crypto::CryptoError local_error = EnsureTpm(/*reload_key=*/false);
  if (!is_cryptohome_key_loaded()) {
    LOG(ERROR) << "Vault keyset is wrapped by the TPM, but the TPM is "
               << "unavailable";
    ReportCryptohomeError(kDecryptAttemptButTpmNotAvailable);
    PopulateError(error, local_error);
    return false;
  }

  // This is a sanity check that the keys still match.
  if (serialized.has_tpm_public_key_hash()) {
    if (!IsTPMPubkeyHash(serialized.tpm_public_key_hash(), error)) {
      LOG(ERROR) << "TPM public key hash mismatch.";
      ReportCryptohomeError(kDecryptAttemptButTpmKeyMismatch);
      return false;
    }
  }

  SecureBlob salt(serialized.salt().begin(), serialized.salt().end());
  SecureBlob tpm_key = GetTpmKeyFromSerialized(serialized, is_pcr_extended);
  bool is_pcr_bound = serialized.flags() & SerializedVaultKeyset::PCR_BOUND;
  if (is_pcr_bound) {
    if (!DecryptTpmBoundToPcr(vault_key, tpm_key, salt, error,
                              &key_out_data->vkk_iv, &key_out_data->vkk_key)) {
      return false;
    }
  } else {
    if (!DecryptTpmNotBoundToPcr(serialized, vault_key, tpm_key, salt, error,
                                 &key_out_data->vkk_iv,
                                 &key_out_data->vkk_key)) {
      return false;
    }
  }

  key_out_data->chaps_iv = key_out_data->vkk_iv;
  key_out_data->authorization_data_iv = key_out_data->vkk_iv;
  key_out_data->wrapped_reset_seed.assign(
      serialized.wrapped_reset_seed().begin(),
      serialized.wrapped_reset_seed().end());

  if (!serialized.has_tpm_public_key_hash() && error) {
    *error = CE_NO_PUBLIC_KEY_HASH;
  }

  return true;
}

bool Crypto::UnwrapVaultKeyset(const SerializedVaultKeyset& serialized,
                               const KeyBlobs& vkk_data,
                               VaultKeyset* keyset,
                               CryptoError* error) const {
  const SecureBlob& vkk_key = vkk_data.vkk_key;
  const SecureBlob& vkk_iv = vkk_data.vkk_iv;
  const SecureBlob& chaps_iv = vkk_data.chaps_iv;
  const SecureBlob& auth_data_iv = vkk_data.authorization_data_iv;

  // Decrypt the keyset protobuf.
  SecureBlob local_encrypted_keyset(serialized.wrapped_keyset().begin(),
                                    serialized.wrapped_keyset().end());
  SecureBlob plain_text;

  if (!CryptoLib::AesDecrypt(local_encrypted_keyset, vkk_key, vkk_iv,
                             &plain_text)) {
    LOG(ERROR) << "AES decryption failed for vault keyset.";
    PopulateError(error, CE_OTHER_CRYPTO);
    return false;
  }
  if (!keyset->FromKeysBlob(plain_text)) {
    LOG(ERROR) << "Failed to decode the keys blob.";
    PopulateError(error, CE_OTHER_CRYPTO);
    return false;
  }

  // Decrypt the chaps key.
  if (serialized.has_wrapped_chaps_key()) {
    SecureBlob local_wrapped_chaps_key(serialized.wrapped_chaps_key());
    SecureBlob unwrapped_chaps_key;

    if (!CryptoLib::AesDecrypt(local_wrapped_chaps_key,
                               vkk_key,
                               chaps_iv,
                               &unwrapped_chaps_key)) {
      LOG(ERROR) << "AES decryption failed for chaps key.";
      PopulateError(error, CE_OTHER_CRYPTO);
      return false;
    }

    keyset->set_chaps_key(unwrapped_chaps_key);
  }

  // Decrypt the reset seed.
  if (!vkk_data.wrapped_reset_seed.empty()) {
    SecureBlob unwrapped_reset_seed;
    SecureBlob local_wrapped_reset_seed =
        SecureBlob(serialized.wrapped_reset_seed());
    SecureBlob local_reset_iv = SecureBlob(serialized.reset_iv());

    if (!CryptoLib::AesDecrypt(local_wrapped_reset_seed, vkk_key,
                               local_reset_iv, &unwrapped_reset_seed)) {
      LOG(ERROR) << "AES decryption failed for reset seed.";
      PopulateError(error, CE_OTHER_CRYPTO);
      return false;
    }

    keyset->set_reset_seed(unwrapped_reset_seed);
  }

  // TODO(kerrnel): Audit if authorization data is used anywhere.
  DecryptAuthorizationData(serialized, keyset, vkk_key, auth_data_iv);

  return true;
}

bool Crypto::DecryptTpmBoundToPcr(const SecureBlob& vault_key,
                                  const SecureBlob& tpm_key,
                                  const SecureBlob& salt,
                                  CryptoError* error,
                                  SecureBlob* vkk_iv,
                                  SecureBlob* vkk_key) const {
  SecureBlob pass_blob(kDefaultPassBlobSize);
  if (!DeriveSecretsSCrypt(vault_key, salt, {&pass_blob, vkk_iv})) {
    return false;
  }

  Tpm::TpmRetryAction retry_action;
  for (int i = 0; i < kTpmDecryptMaxRetries; ++i) {
    std::map<uint32_t, std::string> pcr_map({{kTpmSingleUserPCR, ""}});
    retry_action = tpm_->UnsealWithAuthorization(
        tpm_init_->GetCryptohomeKey(), tpm_key, pass_blob, pcr_map, vkk_key);

    if (retry_action == Tpm::kTpmRetryNone)
      return true;

    if (!TpmErrorIsRetriable(retry_action))
      break;

    // If the error is retriable, reload the key first.
    if (!tpm_init_->ReloadCryptohomeKey()) {
      LOG(ERROR) << "Unable to reload Cryptohome key.";
      break;
    }
  }

  LOG(ERROR) << "Failed to unwrap vkk with creds.";
  *error = TpmErrorToCrypto(retry_action);
  return false;
}

bool Crypto::DecryptTpmNotBoundToPcr(const SerializedVaultKeyset& serialized,
                                     const SecureBlob& vault_key,
                                     const SecureBlob& tpm_key,
                                     const SecureBlob& salt,
                                     CryptoError* error,
                                     SecureBlob* vkk_iv,
                                     SecureBlob* vkk_key) const {
  SecureBlob aes_skey(kDefaultAesKeySize);
  SecureBlob kdf_skey(kDefaultAesKeySize);
  SecureBlob local_vault_key(vault_key.begin(), vault_key.end());
  unsigned int rounds;
  if (serialized.has_password_rounds()) {
    rounds = serialized.password_rounds();
  } else {
    rounds = kDefaultLegacyPasswordRounds;
  }

  bool scrypt_derived =
      serialized.flags() & SerializedVaultKeyset::SCRYPT_DERIVED;
  if (scrypt_derived) {
    if (!DeriveSecretsSCrypt(vault_key, salt, {&aes_skey, &kdf_skey, vkk_iv})) {
      PopulateError(error, CE_OTHER_FATAL);
      return false;
    }
  } else {
    CryptoLib::PasskeyToAesKey(vault_key, salt, rounds, &aes_skey, NULL);
  }

  for (int i = 0; i < kTpmDecryptMaxRetries; i++) {
    Tpm::TpmRetryAction retry_action = tpm_->DecryptBlob(
        tpm_init_->GetCryptohomeKey(),
        tpm_key,
        aes_skey,
        std::map<uint32_t, std::string>(),
        &local_vault_key);

    if (retry_action == Tpm::kTpmRetryNone)
      break;

    if (!TpmErrorIsRetriable(retry_action)) {
      LOG(ERROR) << "Failed to unwrap vkk with creds.";
      ReportCryptohomeError(kDecryptAttemptWithTpmKeyFailed);
      *error = TpmErrorToCrypto(retry_action);
      return false;
    }

    // If the error is retriable, reload the key first.
    if (!tpm_init_->ReloadCryptohomeKey()) {
      LOG(ERROR) << "Unable to reload Cryptohome key.";
      ReportCryptohomeError(kDecryptAttemptWithTpmKeyFailed);
      *error = TpmErrorToCrypto(Tpm::kTpmRetryFailNoRetry);
      return false;
    }
  }

  if (scrypt_derived) {
    *vkk_key = CryptoLib::HmacSha256(kdf_skey, local_vault_key);
  } else {
    if (!CryptoLib::PasskeyToAesKey(local_vault_key,
                                    salt,
                                    rounds,
                                    vkk_key,
                                    vkk_iv)) {
      LOG(ERROR) << "Failure converting IVKK to VKK";
      PopulateError(error, CE_OTHER_FATAL);
      return false;
    }
  }
  return true;
}

bool Crypto::DecryptScryptBlob(const SecureBlob& wrapped_blob,
                               const SecureBlob& key,
                               SecureBlob* blob,
                               CryptoError* error) const {
  int scrypt_rc;
  size_t out_len = 0;

  scrypt_rc = scryptdec_buf(wrapped_blob.data(), wrapped_blob.size(),
                            blob->data(), &out_len, key.data(), key.size(),
                            kScryptMaxMem, 100.0, kScryptMaxDecryptTime);
  if (scrypt_rc) {
    LOG(ERROR) << "Blob Scrypt decryption returned error code: " << scrypt_rc;
    PopulateError(error, CE_SCRYPT_CRYPTO);
    return false;
  }
  // Check if the plaintext is the right length.
  if ((wrapped_blob.size() < kScryptHeaderLength) ||
      (out_len != (wrapped_blob.size() - kScryptHeaderLength))) {
    LOG(ERROR) << "Blob Scrypt decryption output was the wrong length";
    PopulateError(error, CE_SCRYPT_CRYPTO);
    return false;
  }
  blob->resize(out_len);
  return true;
}

bool Crypto::DecryptScrypt(const SerializedVaultKeyset& serialized,
                           const SecureBlob& key,
                           CryptoError* error,
                           VaultKeyset* keyset) const {
  SecureBlob blob = SecureBlob(serialized.wrapped_keyset());
  SecureBlob decrypted(blob.size());
  if (!DecryptScryptBlob(blob, key, &decrypted, error)) {
    LOG(ERROR) << "Wrapped keyset Scrypt decrypt failed.";
    return false;
  }

  bool chaps_key_present = serialized.has_wrapped_chaps_key();
  if (chaps_key_present) {
    SecureBlob chaps_key;
    SecureBlob wrapped_chaps_key = SecureBlob(serialized.wrapped_chaps_key());
    chaps_key.resize(wrapped_chaps_key.size());
    // Perform a Scrypt operation on wrapped chaps key.
    if (!DecryptScryptBlob(wrapped_chaps_key, key, &chaps_key, error)) {
      LOG(ERROR) << "Chaps key scrypt decrypt failed.";
      return false;
    }
    keyset->set_chaps_key(chaps_key);
  }

  if (serialized.has_wrapped_reset_seed()) {
    SecureBlob reset_seed;
    SecureBlob wrapped_reset_seed = SecureBlob(serialized.wrapped_reset_seed());
    reset_seed.resize(wrapped_reset_seed.size());
    // Perform a Scrypt operation on wrapped reset seed.
    if (!DecryptScryptBlob(wrapped_reset_seed, key, &reset_seed, error)) {
      LOG(ERROR) << "Reset seed scrypt decrypt failed.";
      return false;
    }
    keyset->set_reset_seed(reset_seed);
  }

  // There is a SHA hash included at the end of the decrypted blob. However,
  // scrypt already appends a MAC, so if the payload is corrupted we will fail
  // on the first call to DecryptScryptBlob.
  // TODO(crbug.com/984782): get rid of this entirely.
  if (decrypted.size() < SHA_DIGEST_LENGTH) {
    LOG(ERROR) << "Message length underflow: " << decrypted.size() << " bytes?";
    return false;
  }
  decrypted.resize(decrypted.size() - SHA_DIGEST_LENGTH);
  keyset->FromKeysBlob(decrypted);
  return true;
}

bool Crypto::DecryptLECredential(const SerializedVaultKeyset& serialized,
                                 const SecureBlob& vault_key,
                                 KeyBlobs* vkk_data,
                                 SecureBlob* reset_secret,
                                 CryptoError* error) const {
  if (!use_tpm_ || !tpm_)
    return false;

  // Bail immediately if we don't have a valid LECredentialManager.
  if (!le_manager_) {
    PopulateError(error, CE_LE_NOT_SUPPORTED);
    return false;
  }

  CHECK(serialized.flags() & SerializedVaultKeyset::LE_CREDENTIAL);

  SecureBlob le_secret(kDefaultAesKeySize);
  SecureBlob kdf_skey(kDefaultAesKeySize);
  SecureBlob le_iv(kAesBlockSize);
  SecureBlob salt(serialized.salt().begin(), serialized.salt().end());
  if (!DeriveSecretsSCrypt(vault_key, salt, {&le_secret, &kdf_skey, &le_iv})) {
    PopulateError(error, CE_OTHER_FATAL);
    return false;
  }

  vkk_data->authorization_data_iv = le_iv;
  vkk_data->chaps_iv.assign(serialized.le_chaps_iv().begin(),
                            serialized.le_chaps_iv().end());

  // Try to obtain the HE Secret from the LECredentialManager.
  SecureBlob he_secret;
  int ret = le_manager_->CheckCredential(serialized.le_label(), le_secret,
                                         &he_secret, reset_secret);

  if (ret != LE_CRED_SUCCESS) {
    PopulateError(error, ConvertLeError(ret));
    return false;
  }

  vkk_data->vkk_iv.assign(serialized.le_fek_iv().begin(),
                          serialized.le_fek_iv().end());

  SecureBlob vkk_seed = CryptoLib::HmacSha256(
      he_secret, brillo::BlobFromString(kHESecretHmacData));
  vkk_data->vkk_key = CryptoLib::HmacSha256(kdf_skey, vkk_seed);

  return true;
}

bool Crypto::DecryptChallengeCredential(const SerializedVaultKeyset& serialized,
                                        const SecureBlob& key,
                                        CryptoError* error,
                                        VaultKeyset* vault_keyset) const {
  if (!(serialized.flags() & SerializedVaultKeyset::SCRYPT_WRAPPED)) {
    LOG(ERROR) << "Invalid flags for challenge-protected keyset";
    *error = CE_OTHER_FATAL;
    return false;
  }
  return DecryptScrypt(serialized, key, error, vault_keyset);
}

bool Crypto::NeedsPcrBinding(const uint64_t& label) const {
    return le_manager_->NeedsPcrBinding(label);
}

bool Crypto::DecryptVaultKeyset(const SerializedVaultKeyset& serialized,
                                const SecureBlob& vault_key,
                                bool is_pcr_extended,
                                unsigned int* crypt_flags, CryptoError* error,
                                VaultKeyset* vault_keyset) const {
  if (crypt_flags)
    *crypt_flags = serialized.flags();
  PopulateError(error, CE_NONE);

  unsigned int flags = serialized.flags();

  if (flags & SerializedVaultKeyset::LE_CREDENTIAL) {
    SecureBlob reset_secret;
    KeyBlobs vkk_data;
    if (!DecryptLECredential(serialized, vault_key, &vkk_data, &reset_secret,
                             error)) {
      return false;
    }

    // This is possible to be empty if an old version of CR50 is running.
    if (!reset_secret.empty()) {
      vault_keyset->set_reset_secret(reset_secret);
    }

    return UnwrapVaultKeyset(serialized, vkk_data, vault_keyset, error);
  }

  if (flags & SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED) {
    return DecryptChallengeCredential(serialized, vault_key, error,
                                      vault_keyset);
  }

  // For non-LE credentials: Check if the vault keyset was Scrypt-wrapped
  // (start with Scrypt to avoid reaching to TPM if both flags are set)
  if (flags & SerializedVaultKeyset::SCRYPT_WRAPPED) {
    bool should_try_tpm = false;
    if (flags & SerializedVaultKeyset::TPM_WRAPPED) {
      LOG(ERROR) << "Keyset wrapped with both TPM and Scrypt?";
      ReportCryptohomeError(cryptohome::kBothTpmAndScryptWrappedKeyset);
      // Fallback for the bug when both flags were set: try both methods
      should_try_tpm = true;
    }
    if (DecryptScrypt(serialized, vault_key, error, vault_keyset))
      return true;
    if (!should_try_tpm)
      return false;
  }

  if (flags & SerializedVaultKeyset::TPM_WRAPPED) {
    KeyBlobs vkk_data;
    if (!DecryptTPM(serialized, vault_key, is_pcr_extended, error,
                    &vkk_data)) {
      return false;
    }

    if (!UnwrapVaultKeyset(serialized, vkk_data, vault_keyset, error)) {
      return false;
    }

    // By this point we know that the TPM is successfully owned, everything
    // is initialized, and we were able to successfully decrypt a
    // TPM-wrapped keyset. So, for TPMs with updateable firmware, we assume
    // that it is stable (and the TPM can invalidate the old version).
    tpm_->DeclareTpmFirmwareStable();

    return true;
  }

  LOG(ERROR) << "Keyset wrapped with unknown method.";
  return false;
}

bool Crypto::GenerateEncryptedRawKeyset(const VaultKeyset& vault_keyset,
                                        const SecureBlob& vkk_key,
                                        const SecureBlob& fek_iv,
                                        const SecureBlob& chaps_iv,
                                        SecureBlob* cipher_text,
                                        SecureBlob* wrapped_chaps_key) const {
  SecureBlob blob;
  if (!vault_keyset.ToKeysBlob(&blob)) {
    LOG(ERROR) << "Failure serializing keyset to buffer";
    return false;
  }

  SecureBlob chaps_key = vault_keyset.chaps_key();
  if (!CryptoLib::AesEncrypt(blob, vkk_key, fek_iv, cipher_text) ||
      !CryptoLib::AesEncrypt(chaps_key, vkk_key, chaps_iv,
                             wrapped_chaps_key)) {
    LOG(ERROR) << "AES encryption failed.";
    return false;
  }

  return true;
}

bool Crypto::EncryptTPM(const VaultKeyset& vault_keyset,
                        const SecureBlob& key,
                        const SecureBlob& salt,
                        const std::string& obfuscated_username,
                        SerializedVaultKeyset* serialized) const {
  if (!use_tpm_)
    return false;
  EnsureTpm(false);
  if (!is_cryptohome_key_loaded())
    return false;
  SecureBlob vkk_key(kDefaultAesKeySize);
  CryptoLib::GetSecureRandom(vkk_key.data(), vkk_key.size());
  SecureBlob tpm_key;
  SecureBlob extended_tpm_key;
  SecureBlob pass_blob(kDefaultPassBlobSize);
  SecureBlob vkk_iv(kAesBlockSize);

  if (!DeriveSecretsSCrypt(key, salt, { &pass_blob, &vkk_iv })) {
    return false;
  }
  std::map<uint32_t, std::string> default_pcr_map =
      GetPcrMap(obfuscated_username, false /* use_extended_pcr */);
  std::map<uint32_t, std::string> extended_pcr_map =
      GetPcrMap(obfuscated_username, true /* use_extended_pcr */);
  // Encrypt the VKK using the TPM and the user's passkey. The output is two
  // encrypted blobs, sealed to PCR in |tpm_key| and |extended_tpm_key|,
  // which are stored in the serialized vault keyset.
  if (tpm_->SealToPcrWithAuthorization(
        tpm_init_->GetCryptohomeKey(), vkk_key, pass_blob,
        default_pcr_map, &tpm_key) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to wrap vkk with creds.";
    return false;
  }
  if (tpm_->SealToPcrWithAuthorization(
        tpm_init_->GetCryptohomeKey(), vkk_key, pass_blob,
        extended_pcr_map, &extended_tpm_key) != Tpm::kTpmRetryNone) {
    LOG(ERROR) << "Failed to wrap vkk with creds for extended PCR.";
    return false;
  }

  SecureBlob cipher_text;
  SecureBlob wrapped_chaps_key;
  if (!GenerateEncryptedRawKeyset(vault_keyset, vkk_key, vkk_iv, vkk_iv,
                                  &cipher_text, &wrapped_chaps_key)) {
    return false;
  }

  // If a reset seed is present, encrypt and store it, else clear the field.
  if (vault_keyset.reset_seed().size() != 0) {
    SecureBlob reset_iv(kAesBlockSize);
    CryptoLib::GetSecureRandom(reset_iv.data(), reset_iv.size());

    SecureBlob wrapped_reset_seed;
    if (!CryptoLib::AesEncrypt(vault_keyset.reset_seed(), vkk_key, reset_iv,
                               &wrapped_reset_seed)) {
      LOG(ERROR) << "AES encryption of Reset seed failed.";
      return false;
    }
    serialized->set_wrapped_reset_seed(wrapped_reset_seed.data(),
                                       wrapped_reset_seed.size());
    serialized->set_reset_iv(reset_iv.data(), reset_iv.size());
  } else {
    serialized->clear_wrapped_reset_seed();
    serialized->clear_reset_iv();
  }

  // Allow this to fail.  It is not absolutely necessary; it allows us to
  // detect a TPM clear.  If this fails due to a transient issue, then on next
  // successful login, the vault keyset will be re-saved anyway.
  SecureBlob pub_key_hash;
  if (tpm_->GetPublicKeyHash(tpm_init_->GetCryptohomeKey(),
                             &pub_key_hash) == Tpm::kTpmRetryNone)
    serialized->set_tpm_public_key_hash(pub_key_hash.data(),
                                        pub_key_hash.size());

  unsigned int flags = serialized->flags();
  serialized->set_flags((flags & ~SerializedVaultKeyset::SCRYPT_WRAPPED)
                        | SerializedVaultKeyset::TPM_WRAPPED
                        | SerializedVaultKeyset::SCRYPT_DERIVED
                        | SerializedVaultKeyset::PCR_BOUND);
  serialized->set_tpm_key(tpm_key.data(), tpm_key.size());
  serialized->set_extended_tpm_key(extended_tpm_key.data(),
                                   extended_tpm_key.size());
  serialized->set_wrapped_keyset(cipher_text.data(), cipher_text.size());
  if (vault_keyset.chaps_key().size() == CRYPTOHOME_CHAPS_KEY_LENGTH) {
    serialized->set_wrapped_chaps_key(wrapped_chaps_key.data(),
                                      wrapped_chaps_key.size());
  } else {
    serialized->clear_wrapped_chaps_key();
  }

  return EncryptAuthorizationData(serialized, vkk_key, vkk_iv);
}

bool Crypto::EncryptScryptBlob(const SecureBlob& blob,
                               const SecureBlob& key_source,
                               SecureBlob* wrapped_blob) const {
  int scrypt_rc;
  wrapped_blob->resize(blob.size() + kScryptHeaderLength);
  scrypt_rc = scryptenc_buf(blob.data(), blob.size(), wrapped_blob->data(),
                            key_source.data(), key_source.size(), kScryptMaxMem,
                            100.0, scrypt_max_encrypt_time_);
  if (scrypt_rc) {
    LOG(ERROR) << "Blob Scrypt encryption returned error code: " << scrypt_rc;
    return false;
  }
  return true;
}

bool Crypto::EncryptScrypt(const VaultKeyset& vault_keyset,
                           const SecureBlob& key,
                           SerializedVaultKeyset* serialized) const {
  SecureBlob blob;
  if (vault_keyset.IsLECredential()) {
    LOG(ERROR) << "Low entropy credentials cannot be scrypt-wrapped.";
    return false;
  }
  if (!vault_keyset.ToKeysBlob(&blob)) {
    LOG(ERROR) << "Failure serializing keyset to buffer";
    return false;
  }
  // Append the SHA1 hash of the keyset blob. This is done solely for
  // backwards-compatibility purposes, since scrypt already creates a
  // MAC for the encrypted blob. It is ignored in DecryptScrypt since
  // it is redundant.
  SecureBlob hash = CryptoLib::Sha1(blob);
  SecureBlob local_blob = SecureBlob::Combine(blob, hash);
  SecureBlob cipher_text(local_blob.size() + kScryptHeaderLength);

  if (!EncryptScryptBlob(local_blob, key, &cipher_text)) {
    LOG(ERROR) << "Scrypt encrypt of keyset blob failed.";
    return false;
  }

  SecureBlob wrapped_chaps_key;
  if (!EncryptScryptBlob(vault_keyset.chaps_key(), key, &wrapped_chaps_key)) {
    LOG(ERROR) << "Scrypt encrypt of chaps key failed.";
    return false;
  }
  unsigned int flags = serialized->flags();
  serialized->set_flags((flags & ~SerializedVaultKeyset::TPM_WRAPPED)
                        | SerializedVaultKeyset::SCRYPT_WRAPPED);
  serialized->set_wrapped_keyset(cipher_text.data(), cipher_text.size());
  if (vault_keyset.chaps_key().size() == CRYPTOHOME_CHAPS_KEY_LENGTH) {
    serialized->set_wrapped_chaps_key(wrapped_chaps_key.data(),
                                      wrapped_chaps_key.size());
  } else {
    serialized->clear_wrapped_chaps_key();
  }

  // If there is a reset seed, encrypt and store it.
  if (vault_keyset.reset_seed().size() != 0) {
    SecureBlob wrapped_reset_seed;
    if (!EncryptScryptBlob(vault_keyset.reset_seed(), key,
                           &wrapped_reset_seed)) {
      LOG(ERROR) << "Scrypt encrypt of reset seed failed.";
      return false;
    }
    serialized->set_wrapped_reset_seed(wrapped_reset_seed.data(),
                                       wrapped_reset_seed.size());
  } else {
    serialized->clear_wrapped_reset_seed();
  }

  return true;
}

bool Crypto::EncryptLECredential(const VaultKeyset& vault_keyset,
                                 const SecureBlob& key,
                                 const SecureBlob& salt,
                                 const std::string& obfuscated_username,
                                 SerializedVaultKeyset* serialized) const {
  if (!use_tpm_ || !tpm_)
    return false;
  if (!le_manager_)
    return false;

  EnsureTpm(false);

  SecureBlob le_secret(kDefaultAesKeySize);
  SecureBlob kdf_skey(kDefaultAesKeySize);
  SecureBlob le_iv(kAesBlockSize);
  if (!DeriveSecretsSCrypt(key, salt, { &le_secret, &kdf_skey, &le_iv })) {
    return false;
  }

  // Create a randomly generated high entropy secret, derive VKKSeed from it,
  // and use that to generate a VKK. The HE secret will be stored in the
  // LECredentialManager, along with the LE secret (which is |key| here).
  SecureBlob he_secret;
  if (!tpm_->GetRandomDataSecureBlob(kDefaultAesKeySize, &he_secret)) {
    LOG(ERROR) << "Failed to obtain a VKK Seed for LE Credential";
    return false;
  }

  // Derive the VKK_seed by performing an HMAC on he_secret.
  SecureBlob vkk_seed = CryptoLib::HmacSha256(
      he_secret, brillo::BlobFromString(kHESecretHmacData));

  // Generate and store random new IVs for file-encryption keys and
  // chaps key encryption.
  SecureBlob fek_iv(kAesBlockSize);
  SecureBlob chaps_iv(kAesBlockSize);
  CryptoLib::GetSecureRandom(fek_iv.data(), fek_iv.size());
  CryptoLib::GetSecureRandom(chaps_iv.data(), chaps_iv.size());
  serialized->set_le_fek_iv(fek_iv.data(), fek_iv.size());
  serialized->set_le_chaps_iv(chaps_iv.data(), chaps_iv.size());

  SecureBlob cipher_text;
  SecureBlob wrapped_chaps_key;
  SecureBlob vkk_key = CryptoLib::HmacSha256(kdf_skey, vkk_seed);
  if (!GenerateEncryptedRawKeyset(vault_keyset, vkk_key, fek_iv, chaps_iv,
                                  &cipher_text, &wrapped_chaps_key)) {
    return false;
  }

  serialized->set_wrapped_keyset(cipher_text.data(), cipher_text.size());
  if (vault_keyset.chaps_key().size() == CRYPTOHOME_CHAPS_KEY_LENGTH) {
    serialized->set_wrapped_chaps_key(wrapped_chaps_key.data(),
                                      wrapped_chaps_key.size());
  } else {
    serialized->clear_wrapped_chaps_key();
  }

  if (!EncryptAuthorizationData(serialized, vkk_key, le_iv)) {
    return false;
  }

  // Once we are able to correctly set up the VaultKeyset encryption,
  // store the LE and HE credential in the LECredentialManager.

  // Use the default delay schedule for now.
  std::map<uint32_t, uint32_t> delay_sched;
  for (const auto& entry : kDefaultDelaySchedule) {
    delay_sched[entry.attempts] = entry.delay;
  }

  // Generate a unique reset secret for this credential.
  SecureBlob reset_secret;

  if (!vault_keyset.reset_seed().empty()) {
    SecureBlob local_reset_seed(vault_keyset.reset_seed().begin(),
                                vault_keyset.reset_seed().end());
    SecureBlob reset_salt(kAesBlockSize);
    CryptoLib::GetSecureRandom(reset_salt.data(), reset_salt.size());
    serialized->set_reset_salt(reset_salt.data(), reset_salt.size());
    reset_secret = CryptoLib::HmacSha256(reset_salt, local_reset_seed);
  } else if (!vault_keyset.reset_secret().empty()) {
    reset_secret = vault_keyset.reset_secret();
  } else {
    LOG(ERROR) << "The VaultKeyset doesn't have a reset seed, so we can't"
      " set up an LE credential.";
    return false;
  }

  ValidPcrCriteria valid_pcr_criteria;
  if (!GetValidPCRValues(obfuscated_username, &valid_pcr_criteria)) {
    return false;
  }

  uint64_t label;
  int ret =
      le_manager_->InsertCredential(le_secret, he_secret, reset_secret,
                                    delay_sched, valid_pcr_criteria, &label);
  if (ret == LE_CRED_SUCCESS) {
    serialized->set_flags(SerializedVaultKeyset::LE_CREDENTIAL);
    serialized->set_le_label(label);
    serialized->mutable_key_data()->mutable_policy()->set_auth_locked(false);
    return true;
  }

  if (ret == LE_CRED_ERROR_NO_FREE_LABEL) {
    LOG(ERROR)
        << "InsertLECredential failed: No free label available in hash tree.";
  } else if (ret == LE_CRED_ERROR_HASH_TREE) {
    LOG(ERROR) << "InsertLECredential failed: hash tree error.";
  }

  return false;
}

bool Crypto::EncryptChallengeCredential(
    const VaultKeyset& vault_keyset,
    const SecureBlob& key,
    const std::string& obfuscated_username,
    SerializedVaultKeyset* serialized) const {
  serialized->set_flags(serialized->flags() |
                        SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);
  if (!EncryptScrypt(vault_keyset, key, serialized))
    return false;
  DCHECK(!(serialized->flags() & SerializedVaultKeyset::TPM_WRAPPED));
  DCHECK(serialized->flags() & SerializedVaultKeyset::SCRYPT_WRAPPED);
  DCHECK(serialized->flags() &
         SerializedVaultKeyset::SIGNATURE_CHALLENGE_PROTECTED);
  return true;
}

bool Crypto::EncryptAuthorizationData(SerializedVaultKeyset* serialized,
                                      const SecureBlob& vkk_key,
                                      const SecureBlob& vkk_iv) const {
  // Handle AuthorizationData secrets if provided.
  if (serialized->key_data().authorization_data_size() > 0) {
    KeyData* key_data = serialized->mutable_key_data();
    for (int auth_data_i = 0; auth_data_i < key_data->authorization_data_size();
         ++auth_data_i) {
      KeyAuthorizationData* auth_data =
          key_data->mutable_authorization_data(auth_data_i);
      for (int secret_i = 0; secret_i < auth_data->secrets_size(); ++secret_i) {
        KeyAuthorizationSecret* secret = auth_data->mutable_secrets(secret_i);
        // Secrets that are externally provided should not be wrapped when
        // this is called.  However, calling Encrypt() again should be
        // idempotent.  External callers should be filtered at the API layer.
        if (secret->wrapped() || !secret->has_symmetric_key())
          continue;
        SecureBlob clear_auth_key(secret->symmetric_key());
        SecureBlob encrypted_auth_key;

        if (!CryptoLib::AesEncrypt(clear_auth_key, vkk_key, vkk_iv,
                                   &encrypted_auth_key)) {
          LOG(ERROR) << "Failed to wrap a symmetric authorization key:"
                     << " (" << auth_data_i << "," << secret_i << ")";
          // This forces a failure.
          return false;
        }
        secret->set_symmetric_key(encrypted_auth_key.to_string());
        secret->set_wrapped(true);
      }
    }
  }

  return true;
}

void Crypto::DecryptAuthorizationData(const SerializedVaultKeyset& serialized,
                                      VaultKeyset* keyset,
                                      const SecureBlob& vkk_key,
                                      const SecureBlob& vkk_iv) const {
  // Use the same key to unwrap the wrapped authorization data.
  if (serialized.key_data().authorization_data_size() > 0) {
    KeyData* key_data = keyset->mutable_serialized()->mutable_key_data();
    for (int auth_data_i = 0; auth_data_i < key_data->authorization_data_size();
         ++auth_data_i) {
      KeyAuthorizationData* auth_data =
          key_data->mutable_authorization_data(auth_data_i);
      for (int secret_i = 0; secret_i < auth_data->secrets_size(); ++secret_i) {
        KeyAuthorizationSecret* secret = auth_data->mutable_secrets(secret_i);
        if (!secret->wrapped() || !secret->has_symmetric_key())
          continue;
        SecureBlob encrypted_auth_key(secret->symmetric_key());
        SecureBlob clear_key;
        // Is it reasonable to use this key here as well?
        if (!CryptoLib::AesDecrypt(encrypted_auth_key, vkk_key, vkk_iv,
                                   &clear_key)) {
          LOG(ERROR) << "Failed to unwrap a symmetric authorization key:"
                     << " (" << auth_data_i << "," << secret_i << ")";
          // This does not force a failure to use the keyset.
          continue;
        }
        secret->set_symmetric_key(clear_key.to_string());
        secret->set_wrapped(false);
      }
    }
  }
}

bool Crypto::EncryptVaultKeyset(const VaultKeyset& vault_keyset,
                                const SecureBlob& vault_key,
                                const SecureBlob& vault_key_salt,
                                const std::string& obfuscated_username,
                                SerializedVaultKeyset* serialized) const {
  if (vault_keyset.IsLECredential()) {
    if (!EncryptLECredential(vault_keyset, vault_key, vault_key_salt,
                             obfuscated_username, serialized)) {
      // TODO(crbug.com/794010): add ReportCryptohomeError
      return false;
    }
  } else if (vault_keyset.IsSignatureChallengeProtected()) {
    if (!EncryptChallengeCredential(vault_keyset, vault_key,
                                    obfuscated_username, serialized)) {
      // TODO(crbug.com/842791): add ReportCryptohomeError
      return false;
    }
  } else {
    if (!EncryptTPM(vault_keyset, vault_key, vault_key_salt,
                    obfuscated_username, serialized)) {
      if (use_tpm_ && tpm_ && tpm_->IsOwned()) {
        ReportCryptohomeError(kEncryptWithTpmFailed);
      }
      if (!EncryptScrypt(vault_keyset, vault_key, serialized)) {
        return false;
      }
    }
  }

  serialized->set_salt(vault_key_salt.data(), vault_key_salt.size());
  return true;
}

bool Crypto::EncryptWithTpm(const SecureBlob& data,
                            std::string* encrypted_data) const {
  SecureBlob aes_key;
  SecureBlob sealed_key;
  if (!CreateSealedKey(&aes_key, &sealed_key))
    return false;
  return EncryptData(data, aes_key, sealed_key, encrypted_data);
}

bool Crypto::DecryptWithTpm(const std::string& encrypted_data,
                            SecureBlob* data) const {
  SecureBlob aes_key;
  SecureBlob sealed_key;
  if (!UnsealKey(encrypted_data, &aes_key, &sealed_key)) {
    return false;
  }
  return DecryptData(encrypted_data, aes_key, data);
}

bool Crypto::CreateSealedKey(SecureBlob* aes_key,
                             SecureBlob* sealed_key) const {
  if (!use_tpm_)
    return false;
  if (!tpm_->GetRandomDataSecureBlob(kDefaultAesKeySize, aes_key)) {
    LOG(ERROR) << "GetRandomDataSecureBlob failed.";
    return false;
  }
  if (!tpm_->SealToPCR0(*aes_key, sealed_key)) {
    LOG(ERROR) << "Failed to seal cipher key.";
    return false;
  }
  return true;
}

bool Crypto::EncryptData(const SecureBlob& data,
                         const SecureBlob& aes_key,
                         const SecureBlob& sealed_key,
                         std::string* encrypted_data) const {
  if (!use_tpm_)
    return false;
  SecureBlob iv;
  if (!tpm_->GetRandomDataSecureBlob(kAesBlockSize, &iv)) {
    LOG(ERROR) << "GetRandomDataSecureBlob failed.";
    return false;
  }
  SecureBlob encrypted_data_blob;
  if (!CryptoLib::AesEncryptSpecifyBlockMode(data, 0, data.size(), aes_key, iv,
                                             CryptoLib::kPaddingStandard,
                                             CryptoLib::kCbc,
                                             &encrypted_data_blob)) {
    LOG(ERROR) << "Failed to encrypt serial data.";
    return false;
  }
  EncryptedData encrypted_pb;
  encrypted_pb.set_wrapped_key(sealed_key.data(), sealed_key.size());
  encrypted_pb.set_iv(iv.data(), iv.size());
  encrypted_pb.set_encrypted_data(encrypted_data_blob.data(),
                                  encrypted_data_blob.size());
  encrypted_pb.set_mac(CryptoLib::ComputeEncryptedDataHMAC(encrypted_pb,
                                                           aes_key));
  if (!encrypted_pb.SerializeToString(encrypted_data)) {
    LOG(ERROR) << "Could not serialize data to string.";
    return false;
  }
  return true;
}

bool Crypto::UnsealKey(const std::string& encrypted_data,
                       SecureBlob* aes_key,
                       SecureBlob* sealed_key) const {
  if (!use_tpm_)
    return false;
  EncryptedData encrypted_pb;
  if (!encrypted_pb.ParseFromString(encrypted_data)) {
    LOG(ERROR) << "Could not decrypt data as it was not an EncryptedData "
               << "protobuf";
    return false;
  }
  SecureBlob tmp(encrypted_pb.wrapped_key().begin(),
                 encrypted_pb.wrapped_key().end());
  sealed_key->swap(tmp);
  if (!tpm_->Unseal(*sealed_key, aes_key)) {
    LOG(ERROR) << "Cannot unseal aes key.";
    return false;
  }
  return true;
}

bool Crypto::DecryptData(const std::string& encrypted_data,
                         const brillo::SecureBlob& aes_key,
                         brillo::SecureBlob* data) const {
  EncryptedData encrypted_pb;
  if (!encrypted_pb.ParseFromString(encrypted_data)) {
    LOG(ERROR) << "Could not decrypt data as it was not an EncryptedData "
               << "protobuf";
    return false;
  }
  std::string mac = CryptoLib::ComputeEncryptedDataHMAC(encrypted_pb, aes_key);
  if (mac.length() != encrypted_pb.mac().length()) {
    LOG(ERROR) << "Corrupted data in encrypted pb.";
    return false;
  }
  if (0 != brillo::SecureMemcmp(mac.data(), encrypted_pb.mac().data(),
                                  mac.length())) {
    LOG(ERROR) << "Corrupted data in encrypted pb.";
    return false;
  }
  SecureBlob iv(encrypted_pb.iv().begin(), encrypted_pb.iv().end());
  SecureBlob encrypted_data_blob(encrypted_pb.encrypted_data().begin(),
                                 encrypted_pb.encrypted_data().end());
  if (!CryptoLib::AesDecryptSpecifyBlockMode(encrypted_data_blob,
                                             0,
                                             encrypted_data_blob.size(),
                                             aes_key,
                                             iv,
                                             CryptoLib::kPaddingStandard,
                                             CryptoLib::kCbc,
                                             data)) {
    LOG(ERROR) << "Failed to decrypt encrypted data.";
    return false;
  }
  return true;
}

bool Crypto::ResetLECredential(const SerializedVaultKeyset& serialized_reset,
                               CryptoError* error,
                               const VaultKeyset& vk) const {
  if (!use_tpm_ || !tpm_)
    return false;

  // Bail immediately if we don't have a valid LECredentialManager.
  if (!le_manager_) {
    PopulateError(error, CE_LE_NOT_SUPPORTED);
    return false;
  }

  CHECK(serialized_reset.flags() & SerializedVaultKeyset::LE_CREDENTIAL);
  SecureBlob local_reset_seed(vk.reset_seed().begin(), vk.reset_seed().end());
  SecureBlob reset_salt(serialized_reset.reset_salt().begin(),
                        serialized_reset.reset_salt().end());
  if (local_reset_seed.empty() || reset_salt.empty()) {
    LOG(ERROR) << "Reset seed/salt is empty, can't reset LE credential.";
    PopulateError(error, CE_OTHER_FATAL);
    return false;
  }

  SecureBlob reset_secret = CryptoLib::HmacSha256(reset_salt, local_reset_seed);
  int ret =
      le_manager_->ResetCredential(serialized_reset.le_label(), reset_secret);
  if (ret != LE_CRED_SUCCESS) {
    PopulateError(error, ret == LE_CRED_ERROR_INVALID_RESET_SECRET
                             ? CE_LE_INVALID_SECRET
                             : CE_OTHER_FATAL);
    return false;
  }
  return true;
}

bool Crypto::RemoveLECredential(uint64_t label) const {
  if (!use_tpm_ || !tpm_) {
    LOG(WARNING) << "No TPM instance for RemoveLECredential.";
    return false;
  }

  // Bail immediately if we don't have a valid LECredentialManager.
  if (!le_manager_) {
    LOG(ERROR) << "No LECredentialManager instance for RemoveLECredential.";
    return false;
  }

  return le_manager_->RemoveCredential(label) == LE_CRED_SUCCESS;
}

bool Crypto::is_cryptohome_key_loaded() const {
  if (tpm_ == NULL || tpm_init_ == NULL) {
    return false;
  }
  return tpm_init_->HasCryptohomeKey();
}

bool Crypto::GetValidPCRValues(
    const std::string& obfuscated_username,
    ValidPcrCriteria* valid_pcr_criteria) const {
  std::string default_pcr_str = std::string(kDefaultPcrValue, 32);

  // The digest used for validation of PCR values by pinweaver is sha256 of
  // the current PCR value of index 4.
  // Step 1 - calculate expected values of PCR4 initially
  // (kDefaultPcrValue = 0) and after user logins
  // (sha256(initial_value | user_specific_digest)).
  // Step 2 - calculate digest of those values, to support multi-PCR case,
  // where all those expected values for all PCRs are sha256'ed together.
  std::string default_digest = crypto::SHA256HashString(default_pcr_str);

  // The second valid digest is the one obtained from the future value of
  // PCR4, after it's extended by |obfuscated_username|. Compute the value of
  // PCR4 after it will be extended first, which is
  // sha256(default_value + sha256(extend_text)).
  std::string extended_arc_pcr_value = crypto::SHA256HashString(
      default_pcr_str + crypto::SHA256HashString(obfuscated_username));

  // The second valid digest used by pinweaver for validation will be
  // sha256 of the extended value of pcr4.
  std::string extended_digest =
      crypto::SHA256HashString(extended_arc_pcr_value);

  ValidPcrValue default_pcr_value;
  memset(default_pcr_value.bitmask, 0, 2);
  default_pcr_value.bitmask[kTpmSingleUserPCR / 8] = 1u << kTpmSingleUserPCR;
  default_pcr_value.digest = default_digest;
  valid_pcr_criteria->push_back(default_pcr_value);

  ValidPcrValue extended_pcr_value;
  memset(extended_pcr_value.bitmask, 0, 2);
  extended_pcr_value.bitmask[kTpmSingleUserPCR / 8] = 1u << kTpmSingleUserPCR;
  extended_pcr_value.digest = extended_digest;
  valid_pcr_criteria->push_back(extended_pcr_value);

  return true;
}

std::map<uint32_t, std::string> Crypto::GetPcrMap(
    const std::string& obfuscated_username,
    bool use_extended_pcr) const {
  std::map<uint32_t, std::string> pcr_map;
  if (use_extended_pcr) {
    pcr_map[kTpmSingleUserPCR] =
        GetHashDescription(tpm_).GetPcrAfterExtendingUsername(
            SecureBlob(obfuscated_username)).to_string();
  } else {
    pcr_map[kTpmSingleUserPCR] = std::string(GetHashDescription(tpm_).size, 0);
  }
  return pcr_map;
}
}  // namespace cryptohome
