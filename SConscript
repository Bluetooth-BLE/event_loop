# Copyright (c) 2026, RT-Thread Development Team
#
# SPDX-License-Identifier: Apache-2.0
#
# Change Logs:
# Date           Author     Notes
# 2026-04-07     John       first version


from building import *

cwd     = GetCurrentDir()

CPPPATH = [os.path.join(cwd, 'inc')]

src = Split('''
    src/event_loop.c
''')

if GetDepend(['EVENT_LOOP_USING_SAMPLES']):
    src += Glob('samples/event_loop_test.c')
    CPPPATH.append(os.path.join(cwd, 'samples'))

group = DefineGroup(
    'event_loop',
    src,
    depend  = ['PKG_USING_EVENT_LOOP'],
    CPPPATH = CPPPATH,
)

Return('group')
