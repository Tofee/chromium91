// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import type * as Common from '../core/common/common.js';
import * as SDK from '../core/sdk/sdk.js';
import * as UI from '../ui/legacy/legacy.js';
import {IssuesPane} from './IssuesPane.js';

let issueRevealerInstance: IssueRevealer;

export class IssueRevealer implements Common.Revealer.Revealer {
  static instance(opts: {forceNew: boolean|null} = {forceNew: null}): IssueRevealer {
    const {forceNew} = opts;
    if (!issueRevealerInstance || forceNew) {
      issueRevealerInstance = new IssueRevealer();
    }
    return issueRevealerInstance;
  }

  async reveal(issue: Object): Promise<void> {
    if (!(issue instanceof SDK.Issue.Issue)) {
      throw new Error('Internal error: not a issue');
    }
    await UI.ViewManager.ViewManager.instance().showView('issues-pane');
    const view = UI.ViewManager.ViewManager.instance().view('issues-pane');
    if (view) {
      const issuesPane = await view.widget();
      if (issuesPane instanceof IssuesPane) {
        issuesPane.revealByCode(issue.code());
      } else {
        throw new Error('Expected issues pane to be an instance of IssuesPane');
      }
    }
  }
}
