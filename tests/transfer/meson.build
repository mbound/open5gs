# Copyright (C) 2019 by Sukchan Lee <acetcom@gmail.com>

# This file is part of Open5GS.

# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.


testapp_inter_amf_n2_sources = files('''
    abts-inter-amf-n2-main.c
    inter-amf-n2-handover-test.c
'''.split())

testapp_inter_amf_n2_exe = executable('inter-amf-n2',
    sources : testapp_inter_amf_n2_sources,
    c_args : testunit_core_cc_flags,
    dependencies : libtest5gc_dep)

test('inter-amf-n2', testapp_inter_amf_n2_exe,
    is_parallel : false, suite: '5gc')

testapp_transfer_sources = files('''
    abts-main.c
    ue-context-transfer-test.c
'''.split())

testapp_transfer_error_sources = files('''
    abts-error-main.c
    ue-context-transfer-error-case-test.c
'''.split())

testapp_transfer_exe = executable('transfer',
    sources : testapp_transfer_sources,
    c_args : testunit_core_cc_flags,
    dependencies : libtest5gc_dep)

testapp_transfer_error_exe = executable('transfer-error',
    sources : testapp_transfer_error_sources,
    c_args : testunit_core_cc_flags,
    dependencies : libtest5gc_dep)

test('transfer', testapp_transfer_exe, is_parallel : false, suite: '5gc')
test('transfer-error', testapp_transfer_error_exe, is_parallel : false, suite: '5gc')