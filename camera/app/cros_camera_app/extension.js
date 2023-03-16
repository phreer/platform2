// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Promisifies the given function that is using the callback style with
 * chrome.runtime.lastError for error handling.
 * @param {function(*): *} fn A function to be promisified.
 * @return {function(*): !Promise<*>} The promisified function.
 */
function promisifyFunction(fn) {
  const newFn = (...args) => new Promise((resolve, reject) => {
    fn(...args, (result) => {
      const err = chrome.runtime.lastError;
      if (err !== undefined) {
        reject(new Error(err.message));
      } else {
        resolve(result)
      }
    });
  });
  return newFn;
}

/**
 * Promisifies the given object by replacing all methods on it with
 * promisifyFunction(). Non-function attributes are not touched.
 * @param {!Object<string, *>} obj An object to be promisified.
 * @return {!Object<string, *>} The promisified object.
 */
function promisifyObject(obj) {
  return new Proxy(obj, {
    get(target, prop, receiver) {
      const maybeFn = Reflect.get(target, prop, receiver);
      if (typeof maybeFn === 'function') {
        return promisifyFunction(maybeFn).bind(target);
      } else {
        return maybeFn;
      }
    }
  });
}

const autotest = promisifyObject(chrome.autotestPrivate);

class CCA {
  /**
   * Opens the camera app.
   * @returns {!Promise<void>} Resolved when the app is launched.
   */
  async open() {
    // TODO(shik): Check if CCA is already opened.
    await autotest.waitForSystemWebAppsInstall();
    await autotest.launchSystemWebApp(
        'Camera', 'chrome://camera-app/views/main.html');
    // TODO(shik): Wait until the preview is streaming.
  }
}

export const cca = new CCA();