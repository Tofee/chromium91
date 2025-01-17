// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview A helper object used by the Scanning App UI in ash/ to
 * provide access to the ScanningHandler which invokes functions that only exist
 * in chrome/.
 */

import {addSingletonGetter, sendWithPromise} from 'chrome://resources/js/cr.m.js';

import {ScanCompleteAction} from './scanning_app_types.js';

/**
 * @typedef {{
 *   baseName: string,
 *   filePath: string,
 * }}
 */
export let SelectedPath;

/**
 * @typedef {{
 *   sourceType: ash.scanning.mojom.SourceType,
 *   fileType: ash.scanning.mojom.FileType,
 *   colorMode: ash.scanning.mojom.ColorMode,
 *   pageSize: ash.scanning.mojom.PageSize,
 *   resolution: number,
 * }}
 */
let ScanJobSettingsForMetrics;

/** @interface */
export class ScanningBrowserProxy {
  /** Initialize ScanningHandler. */
  initialize() {}

  /**
   * Requests the user to choose the directory to save scans.
   * @return {!Promise<!SelectedPath>}
   */
  requestScanToLocation() {}

  /**
   * Opens the Files app with the file |pathToFile| highlighted.
   * @param {string} pathToFile
   * @return {!Promise<boolean>} True if the file is found and Files app opens.
   */
  showFileInLocation(pathToFile) {}

  /**
   * Returns a localized, pluralized string for |name| based on |count|.
   * @param {string} name
   * @param {number} count
   * @return {!Promise<string>}
   */
  getPluralString(name, count) {}

  /**
   * Records the settings for a scan job.
   * @param {!ScanJobSettingsForMetrics} scanJobSettings
   */
  recordScanJobSettings(scanJobSettings) {}

  /**
   * Returns the MyFiles path for the current user.
   * @return {!Promise<string>}
   */
  getMyFilesPath() {}

  /**
   * Opens the Media app with the files specified in |filePaths|.
   * @param {!Array<string>} filePaths
   */
  openFilesInMediaApp(filePaths) {}

  /**
   * Records the action taken after a completed scan job.
   * @param {!ScanCompleteAction} action
   */
  recordScanCompleteAction(action) {}

  /**
   * Records the number of scan setting changes before a scan is initiated.
   * @param {number} numChanges
   */
  recordNumScanSettingChanges(numChanges) {}
}

/** @implements {ScanningBrowserProxy} */
export class ScanningBrowserProxyImpl {
  /** @override */
  initialize() {
    chrome.send('initialize');
  }

  /** @override */
  requestScanToLocation() {
    return sendWithPromise('requestScanToLocation');
  }

  /** @override */
  showFileInLocation(pathToFile) {
    return sendWithPromise('showFileInLocation', pathToFile);
  }

  /** @override */
  getPluralString(name, count) {
    return sendWithPromise('getPluralString', name, count);
  }

  /** @override */
  recordScanJobSettings(scanJobSettings) {
    chrome.send('recordScanJobSettings', [scanJobSettings]);
  }

  /** @override */
  getMyFilesPath() {
    return sendWithPromise('getMyFilesPath');
  }

  /** @override */
  openFilesInMediaApp(filePaths) {
    chrome.send('openFilesInMediaApp', [filePaths]);
  }

  /** @override */
  recordScanCompleteAction(action) {
    chrome.send('recordScanCompleteAction', [action]);
  }

  /** @override */
  recordNumScanSettingChanges(numChanges) {
    chrome.send('recordNumScanSettingChanges', [numChanges]);
  }
}

// The singleton instance_ can be replaced with a test version of this wrapper
// during testing.
addSingletonGetter(ScanningBrowserProxyImpl);
