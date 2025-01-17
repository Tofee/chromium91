// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as Common from '../../core/common/common.js';
import * as Root from '../../core/root/root.js';  // eslint-disable-line no-unused-vars

import {ContextFlavorListener} from './ContextFlavorListener.js';  // eslint-disable-line no-unused-vars

/** @type {!Context} */
let contextInstance;

/** @typedef {(function(new:Object, ...*):void|function(new:Object, ...?):void)} */
let ConstructorFn;  // eslint-disable-line no-unused-vars

export class Context {
  /**
   * @private
   */
  constructor() {
    /** @type {!Map<!ConstructorFn, !Object>} */
    this._flavors = new Map();
    /** @type {!Map<!ConstructorFn, !Common.ObjectWrapper.ObjectWrapper>} */
    this._eventDispatchers = new Map();
  }

  /**
   * @param {{forceNew: ?boolean}} opts
   */
  static instance(opts = {forceNew: null}) {
    const {forceNew} = opts;
    if (!contextInstance || forceNew) {
      contextInstance = new Context();
    }

    return contextInstance;
  }

  /**
   * @param {(function(new:T, ...*):void|function(new:T, ...?):void)} flavorType
   * @param {?T} flavorValue
   * @template T
   */
  setFlavor(flavorType, flavorValue) {
    const value = this._flavors.get(flavorType) || null;
    if (value === flavorValue) {
      return;
    }
    if (flavorValue) {
      this._flavors.set(flavorType, flavorValue);
    } else {
      this._flavors.delete(flavorType);
    }

    this._dispatchFlavorChange(flavorType, flavorValue);
  }

  /**
   * @param {(function(new:T, ...*):void|function(new:T, ...?):void)} flavorType
   * @param {?T} flavorValue
   * @template T
   */
  _dispatchFlavorChange(flavorType, flavorValue) {
    for (const extension of getRegisteredListeners()) {
      if (extension.contextTypes().includes(flavorType)) {
        extension.loadListener().then(instance => instance.flavorChanged(flavorValue));
      }
    }
    const dispatcher = this._eventDispatchers.get(flavorType);
    if (!dispatcher) {
      return;
    }
    dispatcher.dispatchEventToListeners(Events.FlavorChanged, flavorValue);
  }

  /**
   * @param {!ConstructorFn} flavorType
   * @param {function(!Common.EventTarget.EventTargetEvent):void} listener
   * @param {!Object=} thisObject
   */
  addFlavorChangeListener(flavorType, listener, thisObject) {
    let dispatcher = this._eventDispatchers.get(flavorType);
    if (!dispatcher) {
      dispatcher = new Common.ObjectWrapper.ObjectWrapper();
      this._eventDispatchers.set(flavorType, dispatcher);
    }
    dispatcher.addEventListener(Events.FlavorChanged, listener, thisObject);
  }

  /**
   * @param {!ConstructorFn} flavorType
   * @param {function(!Common.EventTarget.EventTargetEvent):void} listener
   * @param {!Object=} thisObject
   */
  removeFlavorChangeListener(flavorType, listener, thisObject) {
    const dispatcher = this._eventDispatchers.get(flavorType);
    if (!dispatcher) {
      return;
    }
    dispatcher.removeEventListener(Events.FlavorChanged, listener, thisObject);
    if (!dispatcher.hasEventListeners(Events.FlavorChanged)) {
      this._eventDispatchers.delete(flavorType);
    }
  }

  /**
   * @param {(function(new:T, ...*):void|function(new:T, ...?):void)} flavorType
   * @return {?T}
   * @template T
   */
  flavor(flavorType) {
    return /** @type {?T} */ (this._flavors.get(flavorType)) || null;
  }

  /**
   * @return {!Set.<(function(new:Object, ...*):void|function(new:Object, ...?):void)>}
   */
  flavors() {
    return new Set(this._flavors.keys());
  }
}

/** @enum {symbol} */
const Events = {
  FlavorChanged: Symbol('FlavorChanged')
};

/** @type {!Array<!ContextFlavorListenerRegistration>} */
const registeredListeners = [];

/**
 * @param {!ContextFlavorListenerRegistration} registration
 */
export function registerListener(registration) {
  registeredListeners.push(registration);
}

function getRegisteredListeners() {
  return registeredListeners;
}

/**
 * @typedef {{
  *  contextTypes: function(): !Array<?>,
  *  loadListener: function(): !Promise<!ContextFlavorListener>,
  * }}
  */
// @ts-ignore typedef
export let ContextFlavorListenerRegistration;
