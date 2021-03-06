/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/
// Original file Copyright Crytek GMBH or its affiliates, used under license.

#include "stdafx.h"
#include "FragmentSplitter.h"

#include "MannequinDialog.h"

CFragmentSplitter::CFragmentSplitter(QWidget* parent)
    : QSplitter(Qt::Vertical, parent)
{
}

void CFragmentSplitter::showEvent(QShowEvent* event)
{
    if (auto pMannequinDialog = CMannequinDialog::GetCurrentInstance())
    {
        connect(pMannequinDialog->findDockWidget<CFragmentBrowser*>(), &QDockWidget::visibilityChanged, pMannequinDialog, [](bool visible)
            {
                if (!visible)
                {
                    return;
                }
                if (auto pMannequinDialog = CMannequinDialog::GetCurrentInstance())
                {
                    if (!pMannequinDialog->IsPaneSelected<CPreviewerPage*>())
                    {
                        pMannequinDialog->ShowPane<CFragmentEditorPage*>();
                    }
                }
            });
    }
}