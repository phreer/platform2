// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_ERROR_LOCATIONS_H_
#define CRYPTOHOME_ERROR_LOCATIONS_H_

#include "cryptohome/error/cryptohome_error.h"

namespace cryptohome {

namespace error {

// This file defines the various location code used by CryptohomeError
// Each of the location should only be used in one error site.

// This file is generated and maintained by the cryptohome/error/location_db.py
// utility. Please run this command in cros_sdk to update this file:
// $ /mnt/host/source/src/platform2/cryptohome/error/tool/location_db.py
//       --update

// Note that we should prevent the implicit cast of this enum class to
// ErrorLocation so that if the macro is not used, the compiler will catch it.
enum class ErrorLocationSpecifier : CryptohomeError::ErrorLocation {
  // Start of generated content. Do NOT modify after this line.
  /* ./user_session/real_user_session.cc */
  kLocUserSessionMountEphemeralFailed = 100,
  /* ./userdataauth.cc */
  kLocUserDataAuthMountGuestMountPointBusy = 101,
  /* ./userdataauth.cc */
  kLocUserDataAuthMountGuestNoGuestSession = 102,
  /* ./userdataauth.cc */
  kLocUserDataAuthMountGuestSessionMountFailed = 103,
  /* ./userdataauth.cc */
  kLocUserDataAuthNoEphemeralMountForOwner = 104,
  /* ./userdataauth.cc */
  kLocUserDataAuthEphemeralMountWithoutCreate = 105,
  /* ./userdataauth.cc */
  kLocUserDataAuthMountAuthSessionNotFound = 106,
  /* ./userdataauth.cc */
  kLocUserDataAuthMountAuthSessionNotAuthed = 107,
  /* ./userdataauth.cc */
  kLocUserDataAuthMountNoAccountID = 108,
  /* ./userdataauth.cc */
  kLocUserDataAuthMountCantGetPublicMountSalt = 109,
  /* ./userdataauth.cc */
  kLocUserDataAuthMountNoKeySecret = 110,
  /* ./userdataauth.cc */
  kLocUserDataAuthMountCreateNoKey = 111,
  /* ./userdataauth.cc */
  kLocUserDataAuthMountCreateMultipleKey = 112,
  /* ./userdataauth.cc */
  kLocUserDataAuthMountCreateKeyNotSpecified = 113,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptCantStartProcessing = 114,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptOperationAborted = 115,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptNoSignatureSealingBackend = 116,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptNoPubKeySigSize = 117,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptSPKIPubKeyMismatch = 118,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptSaltProcessingFailed = 119,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptNoSalt = 120,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptNoSPKIPubKeyDERWhileProcessingSalt = 121,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptSaltPrefixIncorrect = 122,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptNoSPKIPubKeyDERWhileProcessingSecret = 123,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptCreateUnsealingSessionFailed = 124,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptSaltResponseNoSignature = 125,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptUnsealingResponseNoSignature = 126,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptUnsealFailed = 127,
  /* ./challenge_credentials/challenge_credentials_decrypt_operation.cc */
  kLocChalCredDecryptNoSaltSigAlgoWhileProcessingSalt = 128,
  /* ./userdataauth.cc */
  kUserDataAuthInvalidAuthBlockTypeInCreateKeyBlobs = 129,
  /* ./userdataauth.cc */
  kUserDataAuthInvalidAuthBlockTypeInDeriveKeyBlobs = 130,
  /* ./userdataauth.cc */
  kUserDataAuthNoAuthBlockStateInDeriveKeyBlobs = 131,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockInvalidBlockStateInDerive = 132,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockNoSaltInDerive = 133,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockNoTpmKeyInDerive = 134,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockNoScryptDerivedInDerive = 135,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockTpmNotReadyInDerive = 136,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockNoPubKeyHashInDerive = 137,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockDecryptFailedInDerive = 138,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockNoCryptohomeKeyInCreate = 139,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockScryptDeriveFailedInCreate = 140,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockEncryptFailedInCreate = 141,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockReloadKeyFailedInCreate = 142,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockScryptDeriveFailedInDecrypt = 143,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockDecryptFailedInDecrypt = 144,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockReloadKeyFailedInDecrypt = 145,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockVKKConversionFailedInDecrypt = 146,
  /* ./auth_blocks/pin_weaver_auth_block.cc */
  kLocPinWeaverAuthBlockScryptDeriveFailedInCreate = 147,
  /* ./auth_blocks/pin_weaver_auth_block.cc */
  kLocPinWeaverAuthBlockPCRComputationFailedInCreate = 148,
  /* ./auth_blocks/pin_weaver_auth_block.cc */
  kLocPinWeaverAuthBlockInsertCredentialFailedInCreate = 149,
  /* ./auth_blocks/pin_weaver_auth_block.cc */
  kLocPinWeaverAuthBlockInvalidBlockStateInDerive = 150,
  /* ./auth_blocks/pin_weaver_auth_block.cc */
  kLocPinWeaverAuthBlockNoLabelInDerive = 151,
  /* ./auth_blocks/pin_weaver_auth_block.cc */
  kLocPinWeaverAuthBlockNoSaltInDerive = 152,
  /* ./auth_blocks/pin_weaver_auth_block.cc */
  kLocPinWeaverAuthBlockDeriveScryptFailedInDerive = 153,
  /* ./auth_blocks/pin_weaver_auth_block.cc */
  kLocPinWeaverAuthBlockCheckCredFailedInDerive = 154,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilNoAuthBlockInCreateKeyBlobs = 155,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilCreateFailedInCreateKeyBlobs = 156,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilNoAuthBlockInCreateKeyBlobsAsync = 157,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilNoAuthBlockInDeriveKeyBlobs = 158,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilNoVaultKeysetInDeriveKeyBlobs = 159,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilDeriveFailedInDeriveKeyBlobs = 160,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilNoAuthBlockInDeriveKeyBlobsAsync = 161,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilCHRecoveryUnsupportedInGetAuthBlockWithType = 162,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilMaxValueUnsupportedInGetAuthBlockWithType = 163,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilUnknownUnsupportedInGetAuthBlockWithType = 164,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilNoChalInGetAsyncAuthBlockWithType = 165,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilCHUnsupportedInGetAsyncAuthBlockWithType = 166,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilMaxValueUnsupportedInGetAsyncAuthBlockWithType = 167,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilUnknownUnsupportedInGetAsyncAuthBlockWithType = 168,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilChalCredUnsupportedInCreateKeyBlobsAuthFactor = 169,
  /* ./auth_blocks/auth_block_utility_impl.cc */
  kLocAuthBlockUtilUnsupportedInDeriveKeyBlobs = 170,
  /* ./auth_blocks/cryptohome_recovery_auth_block.cc */
  kLocCryptohomeRecoveryAuthBlockCantCreateRecoveryInCreate = 171,
  /* ./auth_blocks/cryptohome_recovery_auth_block.cc */
  kLocCryptohomeRecoveryAuthBlockGenerateHSMPayloadFailedInCreate = 172,
  /* ./auth_blocks/cryptohome_recovery_auth_block.cc */
  kLocCryptohomeRecoveryAuthBlockScryptDeriveFailedInCreate = 173,
  /* ./auth_blocks/cryptohome_recovery_auth_block.cc */
  kLocCryptohomeRecoveryAuthBlockCborConvFailedInCreate = 174,
  /* ./auth_blocks/cryptohome_recovery_auth_block.cc */
  kLocCryptohomeRecoveryAuthBlockRevocationCreateFailedInCreate = 175,
  /* ./auth_blocks/cryptohome_recovery_auth_block.cc */
  kLocCryptohomeRecoveryAuthBlockInvalidBlockStateInDerive = 176,
  /* ./auth_blocks/cryptohome_recovery_auth_block.cc */
  kLocCryptohomeRecoveryAuthBlockCantCreateRecoveryInDerive = 177,
  /* ./auth_blocks/cryptohome_recovery_auth_block.cc */
  kLocCryptohomeRecoveryAuthBlockDecryptFailedInDerive = 178,
  /* ./auth_blocks/cryptohome_recovery_auth_block.cc */
  kLocCryptohomeRecoveryAuthBlockRecoveryFailedInDerive = 179,
  /* ./auth_blocks/cryptohome_recovery_auth_block.cc */
  kLocCryptohomeRecoveryAuthBlockScryptDeriveFailedInDerive = 180,
  /* ./auth_blocks/cryptohome_recovery_auth_block.cc */
  kLocCryptohomeRecoveryAuthBlockRevocationDeriveFailedInDerive = 181,
  /* ./auth_blocks/async_challenge_credential_auth_block.cc */
  kLocAsyncChalCredAuthBlockNoKeyServiceInCreate = 182,
  /* ./auth_blocks/async_challenge_credential_auth_block.cc */
  kLocAsyncChalCredAuthBlockNoInputUserInCreate = 183,
  /* ./auth_blocks/async_challenge_credential_auth_block.cc */
  kLocAsyncChalCredAuthBlockNoInputAuthInCreate = 184,
  /* ./auth_blocks/async_challenge_credential_auth_block.cc */
  kLocAsyncChalCredAuthBlockNoInputAlgInCreate = 185,
  /* ./auth_blocks/async_challenge_credential_auth_block.cc */
  kLocAsyncChalCredAuthBlockServiceGenerateFailedInCreate = 186,
  /* ./auth_blocks/async_challenge_credential_auth_block.cc */
  kLocAsyncChalCredAuthBlockCannotCreateScryptInCreate = 187,
  /* ./auth_blocks/async_challenge_credential_auth_block.cc */
  kLocAsyncChalCredAuthBlockScryptDerivationFailedInCreate = 188,
  /* ./auth_blocks/async_challenge_credential_auth_block.cc */
  kLocAsyncChalCredAuthBlockNoKeyServiceInDerive = 189,
  /* ./auth_blocks/async_challenge_credential_auth_block.cc */
  kLocAsyncChalCredAuthBlockInvalidBlockStateInDerive = 190,
  /* ./auth_blocks/async_challenge_credential_auth_block.cc */
  kLocAsyncChalCredAuthBlockNoChallengeInfoInDerive = 191,
  /* ./auth_blocks/async_challenge_credential_auth_block.cc */
  kLocAsyncChalCredAuthBlockNoAlgorithmInfoInDerive = 192,
  /* ./auth_blocks/async_challenge_credential_auth_block.cc */
  kLocAsyncChalCredAuthBlockServiceDeriveFailedInDerive = 193,
  /* ./auth_blocks/async_challenge_credential_auth_block.cc */
  kLocAsyncChalCredAuthBlockScryptDeriveFailedInDerive = 194,
  /* ./auth_blocks/tpm_auth_block_utils.cc */
  kLocTpmAuthBlockUtilsCryptohomeKeyLoadFailedInPubkeyHash = 195,
  /* ./auth_blocks/tpm_auth_block_utils.cc */
  kLocTpmAuthBlockUtilsGetPubkeyFailedInPubkeyHash = 196,
  /* ./auth_blocks/tpm_auth_block_utils.cc */
  kLocTpmAuthBlockUtilsHashIncorrectInPubkeyHash = 197,
  /* ./auth_blocks/tpm_auth_block_utils.cc */
  kLocTpmAuthBlockUtilsNoTpmKeyInCheckReadiness = 198,
  /* ./auth_blocks/tpm_auth_block_utils.cc */
  kLocTpmAuthBlockUtilsTpmNotOwnedInCheckReadiness = 199,
  /* ./auth_blocks/tpm_auth_block_utils.cc */
  kLocTpmAuthBlockUtilsNoCryptohomeKeyInCheckReadiness = 200,
  /* ./auth_blocks/tpm_auth_block_utils.cc */
  kLocTpmAuthBlockUtilsCHKeyMismatchInCheckReadiness = 201,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockNoUserInputInCreate = 202,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockNoUsernameInCreate = 203,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockNoCryptohomeKeyInCreate = 204,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockScryptDeriveFailedInCreate = 205,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockGetAuthFailedInCreate = 206,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockReloadKeyFailedInCreate = 207,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockDefaultSealFailedInCreate = 208,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockExtendedSealFailedInCreate = 209,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockNoUserInputInDerive = 210,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockInvalidBlockStateInDerive = 211,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockNoScryptDerivedInDerive = 212,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockNotScryptDerivedInDerive = 213,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockNoSaltInDerive = 214,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockNoTpmKeyInDerive = 215,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockNoExtendedTpmKeyInDerive = 216,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockTpmNotReadyInDerive = 217,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockNoPubKeyHashInDerive = 218,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockDecryptFailedInDerive = 219,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockScryptDeriveFailedInDecrypt = 220,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockPreloadFailedInDecrypt = 221,
  /* ./auth_blocks/tpm_bound_to_pcr_auth_block.cc */
  kLocTpmBoundToPcrAuthBlockUnsealFailedInDecrypt = 222,
  /* ./auth_blocks/double_wrapped_compat_auth_block.cc */
  kLocDoubleWrappedAuthBlockUnsupportedInCreate = 223,
  /* ./auth_blocks/double_wrapped_compat_auth_block.cc */
  kLocDoubleWrappedAuthBlockInvalidBlockStateInDerive = 224,
  /* ./auth_blocks/double_wrapped_compat_auth_block.cc */
  kLocDoubleWrappedAuthBlockTpmDeriveFailedInDerive = 225,
  /* ./auth_blocks/challenge_credential_auth_block.cc */
  kLocChalCredAuthBlockCreateScryptAuthBlockFailedInCreate = 226,
  /* ./auth_blocks/challenge_credential_auth_block.cc */
  kLocChalCredAuthBlockDerivationFailedInCreate = 227,
  /* ./auth_blocks/challenge_credential_auth_block.cc */
  kLocChalCredAuthBlockInvalidBlockStateInDerive = 228,
  /* ./auth_blocks/challenge_credential_auth_block.cc */
  kLocChalCredAuthBlockScryptDeriveFailedInDerive = 229,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockRetryLimitExceededInCreate = 230,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockCryptohomeKeyLoadFailedInCreate = 231,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockSaltWrongSizeInCreate = 232,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockSVKKMDerivedFailedInCreate = 233,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockGetAuthFailedInCreate = 234,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockCryptohomeKeyReloadFailedInCreate = 235,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockPersistentGetAuthFailedInCreate = 236,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockSVKKMWrongSizeInCreate = 237,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockHVKKMWrongSizeInCreate = 238,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockVKKWrongSizeInCreate = 239,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockHVKKMSealFailedInCreate = 240,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockHVKKMExtendedSealFailedInCreate = 241,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockGetPubkeyHashFailedInCreate = 242,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockInvalidBlockStateInDerive = 243,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockLoadKeyFailedInDerive = 244,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockTpmNotReadyInDerive = 245,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockCantDeriveVKKInDerive = 246,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockPreloadFailedInDeriveVKK = 247,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockScryptDeriveFailedInDeriveVKK = 248,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockWrongSVKKMSizeInDeriveVKK = 249,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockDeriveHVKKMFailedInDeriveVKK = 250,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockWrongHVKKMSizeInDeriveVKK = 251,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockWrongVKKSizeInDeriveVKK = 252,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockGetAuthFailedInDeriveHVKKM = 253,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockReloadKeyFailedInDeriveHVKKM = 254,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockPersistentGetAuthFailedInDeriveHVKKM = 255,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockUnsealFailedInDeriveHVKKM = 256,
  /* ./auth_blocks/libscrypt_compat_auth_block.cc */
  kLocScryptCompatAuthBlockScryptFailedInCreateHelper = 257,
  /* ./auth_blocks/libscrypt_compat_auth_block.cc */
  kLocScryptCompatAuthBlockParseFailedInParseHeader = 258,
  /* ./auth_blocks/libscrypt_compat_auth_block.cc */
  kLocScryptCompatAuthBlockScryptFailedInParseHeader = 259,
  /* ./auth_blocks/libscrypt_compat_auth_block.cc */
  kLocScryptCompatAuthBlockInputKeyFailedInCreate = 260,
  /* ./auth_blocks/libscrypt_compat_auth_block.cc */
  kLocScryptCompatAuthBlockChapsKeyFailedInCreate = 261,
  /* ./auth_blocks/libscrypt_compat_auth_block.cc */
  kLocScryptCompatAuthBlockResetKeyFailedInCreate = 262,
  /* ./auth_blocks/libscrypt_compat_auth_block.cc */
  kLocScryptCompatAuthBlockInvalidBlockStateInDerive = 263,
  /* ./auth_blocks/libscrypt_compat_auth_block.cc */
  kLocScryptCompatAuthBlockNoWrappedKeysetInDerive = 264,
  /* ./auth_blocks/libscrypt_compat_auth_block.cc */
  kLocScryptCompatAuthBlockInputKeyInDerive = 265,
  /* ./auth_blocks/libscrypt_compat_auth_block.cc */
  kLocScryptCompatAuthBlockChapsKeyInDerive = 266,
  /* ./auth_blocks/libscrypt_compat_auth_block.cc */
  kLocScryptCompatAuthBlockResetKeyInDerive = 267,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManInvalidTreeInInsertCred = 268,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManLabelUnavailableInInsertCred = 269,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManEmptyAuxInInsertCred = 270,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManTpmFailedInInsertCred = 271,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManStoreFailedInInsertCred = 272,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManInvalidTreeInRemoveCred = 273,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManRetrieveLabelFailedInRemoveCred = 274,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManRemoveCredFailedInRemoveCred = 275,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManRemoveLabelFailedInRemoveCred = 276,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManInvalidTreeInCheckSecret = 277,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManInvalidMetadataInCheckSecret = 278,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManStoreLabelFailedInCheckSecret = 279,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManTpmFailedInCheckSecret = 280,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManTreeGetDataFailedInRetrieveLabel = 281,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManNonexistentInRetrieveLabel = 282,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManEmptyAuxInRetrieveLabel = 283,
  /* ./le_credential_manager_impl.cc */
  kLocLECredManConvertTpmError = 284,
  /* ./userdataauth.cc */
  kLocUserDataAuthCreateFailedInStartAuthSession = 285,
  /* ./userdataauth.cc */
  kLocUserDataAuthSessionNotFoundInAuthAuthSession = 286,
  /* ./userdataauth.cc */
  kLocUserDataAuthAuthFailedInAuthAuthSession = 287,
  /* ./userdataauth.cc */
  kLocUserDataAuthSessionNotFoundInExtendAuthSession = 288,
  /* ./userdataauth.cc */
  kLocUserDataAuthExtendFailedInExtendAuthSession = 289,
  /* ./userdataauth.cc */
  kLocUserDataAuthSessionNotFoundInCreatePersistentUser = 290,
  /* ./userdataauth.cc */
  kLocUserDataAuthUserExistsInCreatePersistentUser = 291,
  /* ./userdataauth.cc */
  kLocUserDataAuthCheckExistsFailedInCreatePersistentUser = 292,
  /* ./userdataauth.cc */
  kLocUserDataAuthCreateFailedInCreatePersistentUser = 293,
  /* ./userdataauth.cc */
  kLocUserDataAuthFinalizeFailedInCreatePersistentUser = 294,
  /* ./userdataauth.cc */
  kLocUserDataAuthSessionNotFoundInAuthAuthFactor = 295,
  /* ./auth_session.cc */
  kLocAuthSessionTimedOutInExtend = 296,
  /* ./auth_session.cc */
  kLocAuthSessionCreateUSSFailedInOnUserCreated = 297,
  /* ./auth_session.cc */
  kLocAuthSessionGetCredFailedInAddCred = 298,
  /* ./auth_session.cc */
  kLocAuthSessionKioskKeyNotAllowedInAddCred = 299,
  /* ./auth_session.cc */
  kLocAuthSessionNotAuthedYetInAddCred = 300,
  /* ./auth_session.cc */
  kLocAuthSessionInvalidBlockTypeInAddKeyset = 301,
  /* ./auth_session.cc */
  kLocAuthSessionChalCredUnsupportedInAddKeyset = 302,
  /* ./auth_session.cc */
  kLocAuthSessionPinweaverUnsupportedInAddKeyset = 303,
  /* ./auth_session.cc */
  kLocAuthSessionNullParamInCallbackInAddKeyset = 304,
  /* ./auth_session.cc */
  kLocAuthSessionCreateFailedInAddKeyset = 305,
  /* ./auth_session.cc */
  kLocAuthSessionAddFailedInAddKeyset = 306,
  /* ./auth_session.cc */
  kLocAuthSessionNoFSKeyInAddKeyset = 307,
  /* ./auth_session.cc */
  kLocAuthSessionNoChallengeInfoInAddKeyset = 308,
  /* ./auth_session.cc */
  kLocAuthSessionAddInitialFailedInAddKeyset = 309,
  /* ./auth_session.cc */
  kLocAuthSessionGetCredFailedInUpdate = 310,
  /* ./auth_session.cc */
  kLocAuthSessionUnsupportedKioskKeyInUpdate = 311,
  /* ./auth_session.cc */
  kLocAuthSessionLabelMismatchInUpdate = 312,
  /* ./auth_session.cc */
  kLocAuthSessionUnauthedInUpdate = 313,
  /* ./auth_session.cc */
  kLocAuthSessionInvalidBlockTypeInUpdate = 314,
  /* ./auth_session.cc */
  kLocAuthSessionChalCredUnsupportedInUpdate = 315,
  /* ./auth_session.cc */
  kLocAuthSessionNullParamInCallbackInUpdateKeyset = 316,
  /* ./auth_session.cc */
  kLocAuthSessionCreateFailedInUpdateKeyset = 317,
  /* ./auth_session.cc */
  kLocAuthSessionUpdateWithBlobFailedInUpdateKeyset = 318,
  /* ./auth_session.cc */
  kLocAuthSessionGetCredFailedInAuth = 319,
  /* ./auth_session.cc */
  kLocAuthSessionUnsupportedKeyTypesInAuth = 320,
  /* ./auth_session.cc */
  kLocAuthSessionGetValidKeysetFailedInAuth = 321,
  /* ./auth_session.cc */
  kLocAuthSessionFactorNotFoundInAuthAuthFactor = 322,
  /* ./auth_session.cc */
  kLocAuthSessionInputParseFailedInAuthAuthFactor = 323,
  /* ./auth_session.cc */
  kLocAuthSessionUSSAuthFailedInAuthAuthFactor = 324,
  /* ./auth_session.cc */
  kLocAuthSessionVKConverterFailedInAuthAuthFactor = 325,
  /* ./auth_session.cc */
  kLocAuthSessionInvalidBlockTypeInAuthViaVaultKey = 326,
  /* ./auth_session.cc */
  kLocAuthSessionBlockStateMissingInAuthViaVaultKey = 327,
  /* ./auth_session.cc */
  kLocAuthSessionNullParamInCallbackInLoadVaultKeyset = 328,
  /* ./auth_session.cc */
  kLocAuthSessionDeriveFailedInLoadVaultKeyset = 329,
  /* ./auth_session.cc */
  kLocAuthSessionGetValidKeysetFailedInLoadVaultKeyset = 330,
  /* ./auth_session.cc */
  kLocAuthSessionNonEmptyKioskKeyInGetCred = 331,
  /* ./auth_session.cc */
  kLocAuthSessionEmptyPublicMountKeyInGetCred = 332,
  /* ./auth_session.cc */
  kLocAuthSessionUnauthedInAddAuthFactor = 333,
  /* ./auth_session.cc */
  kLocAuthSessionUnknownFactorInAddAuthFactor = 334,
  /* ./auth_session.cc */
  kLocAuthSessionNoInputInAddAuthFactor = 335,
  /* ./auth_session.cc */
  kLocAuthSessionVKUnsupportedInAddAuthFactor = 336,
  /* ./auth_session.cc */
  kLocAuthSessionCreateAuthFactorFailedInAddViaUSS = 337,
  /* ./auth_session.cc */
  kLocAuthSessionDeriveUSSSecretFailedInAddViaUSS = 338,
  /* ./auth_session.cc */
  kLocAuthSessionAddMainKeyFailedInAddViaUSS = 339,
  /* ./auth_session.cc */
  kLocAuthSessionEncryptFailedInAddViaUSS = 340,
  /* ./auth_session.cc */
  kLocAuthSessionPersistFactorFailedInAddViaUSS = 341,
  /* ./auth_session.cc */
  kLocAuthSessionPersistUSSFailedInAddViaUSS = 342,
  /* ./auth_session.cc */
  kLocAuthSessionAuthFactorAuthFailedInAuthUSS = 343,
  /* ./auth_session.cc */
  kLocAuthSessionLoadUSSFailedInAuthUSS = 344,
  /* ./auth_session.cc */
  kLocAuthSessionDeriveUSSSecretFailedInLoadUSS = 345,
  /* ./auth_session.cc */
  kLocAuthSessionLoadUSSFailedInLoadUSS = 346,
  /* ./auth_session.cc */
  kLocAuthSessionDecryptUSSFailedInLoadUSS = 347,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockNoUserInputInDerive = 348,
  /* ./auth_blocks/tpm_not_bound_to_pcr_auth_block.cc */
  kLocTpmNotBoundToPcrAuthBlockNoUserInputInCreate = 349,
  /* ./auth_factor/auth_factor_manager.cc */
  kLocAuthFactorManagerWrongTypeStringInSave = 350,
  /* ./auth_factor/auth_factor_manager.cc */
  kLocAuthFactorManagerInvalidLabelInSave = 351,
  /* ./auth_factor/auth_factor_manager.cc */
  kLocAuthFactorManagerSerializeFailedInSave = 352,
  /* ./auth_factor/auth_factor_manager.cc */
  kLocAuthFactorManagerWriteFailedInSave = 353,
  /* ./auth_factor/auth_factor_manager.cc */
  kLocAuthFactorManagerWrongTypeStringInLoad = 354,
  /* ./auth_factor/auth_factor_manager.cc */
  kLocAuthFactorManagerReadFailedInLoad = 355,
  /* ./auth_factor/auth_factor_manager.cc */
  kLocAuthFactorManagerParseFailedInLoad = 356,
  /* ./auth_factor/auth_factor.cc */
  kLocAuthFactorCreateKeyBlobsFailedInCreate = 357,
  /* ./auth_factor/auth_factor.cc */
  kLocAuthFactorDeriveFailedInAuth = 358,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockNoUserInputInCreate = 359,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockNoUsernameInCreate = 360,
  /* ./auth_blocks/tpm_ecc_auth_block.cc */
  kLocTpmEccAuthBlockNoUserInputInDerive = 361,
  /* ./vault_keyset.cc */
  kLocVaultKeysetNoResetSeedInEncryptEx = 362,
  /* ./vault_keyset.cc */
  kLocVaultKeysetWrapScryptFailedInEncryptEx = 363,
  /* ./vault_keyset.cc */
  kLocVaultKeysetWrapAESDFailedInEncryptEx = 364,
  /* ./vault_keyset.cc */
  kLocVaultKeysetNotLoadedInDecryptEx = 365,
  /* ./vault_keyset.cc */
  kLocVaultKeysetNotLoadedInDecrypt = 366,
  /* ./vault_keyset.cc */
  kLocVaultKeysetNoBlockStateInDecryptVK = 367,
  /* ./vault_keyset.cc */
  kLocVaultKeysetUnknownBlockTypeInDecryptVK = 368,
  /* ./vault_keyset.cc */
  kLocVaultKeysetDeriveFailedInDecryptVK = 369,
  /* ./vault_keyset.cc */
  kLocVaultKeysetUnwrapVKFailedInDecryptVK = 370,
  /* ./vault_keyset.cc */
  kLocVaultKeysetKeysetDecryptFailedInUnwrapVKK = 371,
  /* ./vault_keyset.cc */
  kLocVaultKeysetKeysetParseFailedInUnwrapVKK = 372,
  /* ./vault_keyset.cc */
  kLocVaultKeysetChapsDecryptFailedInUnwrapVKK = 373,
  /* ./vault_keyset.cc */
  kLocVaultKeysetResetSeedDecryptFailedInUnwrapVKK = 374,
  /* ./vault_keyset.cc */
  kLocVaultKeysetKeysetDecryptFailedInUnwrapScrypt = 375,
  /* ./vault_keyset.cc */
  kLocVaultKeysetChapsDecryptFailedInUnwrapScrypt = 376,
  /* ./vault_keyset.cc */
  kLocVaultKeysetResetSeedDecryptFailedInUnwrapScrypt = 377,
  /* ./vault_keyset.cc */
  kLocVaultKeysetBlobUnderflowInUnwrapScrypt = 378,
  /* ./vault_keyset.cc */
  kLocVaultKeysetKeysetParseFailedInUnwrapScrypt = 379,
  /* ./vault_keyset.cc */
  kLocVaultKeysetMissingFieldInWrapAESD = 380,
  /* ./vault_keyset.cc */
  kLocVaultKeysetSerializationFailedInWrapAESD = 381,
  /* ./vault_keyset.cc */
  kLocVaultKeysetEncryptFailedInWrapAESD = 382,
  /* ./vault_keyset.cc */
  kLocVaultKeysetEncryptChapsFailedInWrapAESD = 383,
  /* ./vault_keyset.cc */
  kLocVaultKeysetEncryptResetSeedInWrapAESD = 384,
  /* ./vault_keyset.cc */
  kLocVaultKeysetLENotSupportedInWrapScrypt = 385,
  /* ./vault_keyset.cc */
  kLocVaultKeysetSerializeFailedInWrapScrypt = 386,
  /* ./vault_keyset.cc */
  kLocVaultKeysetEncryptKeysetFailedInWrapScrypt = 387,
  /* ./vault_keyset.cc */
  kLocVaultKeysetEncryptChapsFailedInWrapScrypt = 388,
  /* ./vault_keyset.cc */
  kLocVaultKeysetEncryptResetSeedFailedInWrapScrypt = 389,
  /* ./vault_keyset.cc */
  kLocVaultKeysetUnwrapVKKFailedInUnwrapVK = 390,
  /* ./vault_keyset.cc */
  kLocVaultKeysetUnwrapScryptFailedInUnwrapVK = 391,
  /* ./vault_keyset.cc */
  kLocVaultKeysetInvalidCombinationInUnwrapVK = 392,
  /* ./vault_keyset.cc */
  kLocVaultKeysetNoResetSeedInEncrypt = 393,
  /* ./vault_keyset.cc */
  kLocVaultKeysetUnknownBlockTypeInEncryptVK = 394,
  /* ./vault_keyset.cc */
  kLocVaultKeysetCreateFailedInEncryptVK = 395,
  /* ./vault_keyset.cc */
  kLocVaultKeysetWrapScryptFailedInEncryptVK = 396,
  /* ./vault_keyset.cc */
  kLocVaultKeysetWrapAESDFailedInEncryptVK = 397,
  /* ./challenge_credentials/challenge_credentials_operation.cc */
  kLocChalCredOperationNoResponseInOnSigResponse = 398,
  /* ./challenge_credentials/challenge_credentials_operation.cc */
  kLocChalCredOperationResponseInvalidInOnSigResponse = 399,
  /* ./challenge_credentials/challenge_credentials_verify_key_operation.cc */
  kLocChalCredVerifyNoAlgorithm = 400,
  /* ./challenge_credentials/challenge_credentials_verify_key_operation.cc */
  kLocChalCredVerifyNoAlgorithmChosen = 401,
  /* ./challenge_credentials/challenge_credentials_verify_key_operation.cc */
  kLocChalCredVerifyGetRandomFailed = 402,
  /* ./challenge_credentials/challenge_credentials_verify_key_operation.cc */
  kLocChalCredVerifyAborted = 403,
  /* ./challenge_credentials/challenge_credentials_verify_key_operation.cc */
  kLocChalCredVerifyChallengeFailed = 404,
  /* ./challenge_credentials/challenge_credentials_verify_key_operation.cc */
  kLocChalCredVerifyInvalidSignature = 405,
  /* ./challenge_credentials/challenge_credentials_helper_impl.cc */
  kLocChalCredHelperConcurrencyNotAllowed = 406,
  /* ./challenge_credentials/challenge_credentials_generate_new_operation.cc */
  kLocChalCredNewAborted = 407,
  /* ./challenge_credentials/challenge_credentials_generate_new_operation.cc */
  kLocChalCredNewNoBackend = 408,
  /* ./challenge_credentials/challenge_credentials_generate_new_operation.cc */
  kLocChalCredNewNoAlgorithm = 409,
  /* ./challenge_credentials/challenge_credentials_generate_new_operation.cc */
  kLocChalCredNewGenerateRandomSaltFailed = 410,
  /* ./challenge_credentials/challenge_credentials_generate_new_operation.cc */
  kLocChalCredNewCantChooseSaltSignatureAlgorithm = 411,
  /* ./challenge_credentials/challenge_credentials_generate_new_operation.cc */
  kLocChalCredNewSealFailed = 412,
  /* ./key_challenge_service_impl.cc */
  kLocKeyChallengeServiceEmptyResponseInChallengeKey = 413,
  /* ./key_challenge_service_impl.cc */
  kLocKeyChallengeServiceParseFailedInChallengeKey = 414,
  /* ./key_challenge_service_impl.cc */
  kLocKeyChallengeServiceKnownDBusErrorInChallengeKey = 415,
  /* ./key_challenge_service_impl.cc */
  kLocKeyChallengeServiceUnknownDBusErrorInChallengeKey = 416,
  /* ./key_challenge_service_impl.cc */
  kLocKeyChallengeServiceInvalidDBusNameInChallengeKey = 417,
  /* ./userdataauth.cc */
  kLocUserDataAuthChalCredFailedInChalRespMount = 418,
  /* ./userdataauth.cc */
  kLocUserDataAuthNoSessionInTryLiteChalRespCheckKey = 419,
  /* ./userdataauth.cc */
  kLocUserDataAuthNoServiceInTryLiteChalRespCheckKey = 420,
  /* ./userdataauth.cc */
  kLocUserDataAuthNoKeyInfoInTryLiteChalRespCheckKey = 421,
  /* ./userdataauth.cc */
  kLocUserDataAuthMultipleKeyInTryLiteChalRespCheckKey = 422,
  // End of generated content.
};
// The enum value should not exceed 65535, otherwise we need to adjust the way
// Unified Error Code is allocated in cryptohome/error/cryptohome_tpm_error.h
// and libhwsec/error/tpm_error.h so that “Cryptohome.Error.LeafErrorWithTPM”
// UMA will continue to work.

}  // namespace error

}  // namespace cryptohome

#endif  // CRYPTOHOME_ERROR_LOCATIONS_H_
