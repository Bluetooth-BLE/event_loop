# Copyright (c) 2026, RT-Thread Development Team
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author     Notes
# 2026-04-07     John       first version

import os
from building import *

cwd     = GetCurrentDir()
src_dir = os.path.join(cwd, 'src')
inc_dir = os.path.join(cwd, 'inc')

src = [os.path.join(src_dir, 'event_loop.c')]

samples_dir = os.path.join(cwd, 'samples')
cpppath = [inc_dir]
if GetDepend('EVENT_LOOP_USING_SAMPLES'):
    src.append(os.path.join(samples_dir, 'event_loop_test.c'))
    cpppath.append(samples_dir)

group = DefineGroup(
    'event_loop',
    src,
    depend  = ['PKG_USING_EVENT_LOOP'],
    CPPPATH = cpppath,
)

Return('group')
