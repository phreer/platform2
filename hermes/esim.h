// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_ESIM_H_
#define HERMES_ESIM_H_

#include <cstdint>
#include <vector>

#include <base/callback.h>
#include <base/memory/weak_ptr.h>

namespace hermes {

// EsimError will be the generic abstraction from specific protocol errors to an
// error that the interface can understand. This should contain a set of errors
// that are recoverable, meaning if they occur, the process can still complete
// successfully. This should also include errors that are fatal, and should be
// reported back to the user.
enum class EsimError {
  kEsimSuccess,       // success condition
  kEsimError,         // fatal
  kEsimNotConnected,  // non-fatal
};

// Provides an interface through which the LPD can communicate with the eSIM
// chip. This is responsible for opening, maintaining, and closing the logical
// channel that will be opened to the chip.
class Esim {
 public:
  using DataBlob = std::vector<uint8_t>;
  using ErrorCallback = base::Callback<void(EsimError error_data)>;
  using DataCallback = base::Callback<void(const DataBlob& esim_data)>;

  virtual ~Esim() = default;

  // Makes eSIM API call to request the eSIM to return either the info1 or
  // the info2 block of data to send to the SM-DP+ server to begin
  // Authentication. Calls |callback| with the newly returned data, or
  // |error_callback| on error.
  //
  // Parameters
  //  which          - specify either the info1 or info2 data
  //  callback       - function to call on successful return of specified eSIM
  //                   info data
  //  error_callback - function to handle error returned from eSIM
  virtual void GetInfo(int which,
                       const DataCallback& data_callback,
                       const ErrorCallback& error_callback) = 0;
  // Makes eSIM API call to request the eSIM to return the eSIM Challenge,
  // which is the second parameter needed to begin Authentication with the
  // SM-DP+ server. Calls |callback| on returned challenge, or |error_callback|
  // on error.
  //
  // Parameters
  //  callback       - function to call on successful return of eSIM challenge
  //                   data blob
  //  error_callback - function to handle error returned from eSIM
  virtual void GetChallenge(const DataCallback& callback,
                            const ErrorCallback& error_callback) = 0;

  // Makes eSIM API call to request eSIM to authenticate server's signature,
  // which is supplied in server_signature. On success, call |callback| with
  // the eSIM's response. If the authentication fails, call |error_callback|.
  //
  // Parameters
  //  server_data    - data that has been signed with server_signature
  //  callback       - function to call with the data returned from the eSIM on
  //                   successful authentication of |server_data|
  //  error_callback - function to call if |server_data| is determined to be
  //                   invalid by eSIM chip
  virtual void AuthenticateServer(const DataBlob& server_data,
                                  const DataCallback& data_callback,
                                  const ErrorCallback& error_callback) = 0;

 protected:
  friend class EsimQmiImplTest;

  // Open logical channel on |slot| to the eSIM. The slot specified should be
  // the one associated with the eSIM chip.
  //
  // Parameters
  //  slot          - slot on which to open the logical channel
  //  callback      - function to call once the channel has been successfully
  //                  opened
  //  error_callack - function handle error if eSIM fails to open channel
  virtual void OpenChannel(const uint8_t slot,
                           const ErrorCallback& error_callback) = 0;

  virtual void OnOpenChannel(const DataBlob& return_data) = 0;

  // Close the channel opened in Esim::OpenChannel.
  virtual void CloseChannel() = 0;
};

}  // namespace hermes

#endif  // HERMES_ESIM_H_
