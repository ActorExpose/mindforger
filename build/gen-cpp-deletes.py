#!/usr/bin/env python
#
# MindForger thinking notebook
#
# Copyright (C) 2016-2021 Martin Dvorak <martin.dvorak@mindforger.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

import sys

if len(sys.argv) > 1:
    c = sys.argv[1]
    print '{}(const {}&) = delete;'.format(c,c)
    print '{}(const {}&&) = delete;'.format(c,c)
    print '{} &operator=(const {}&) = delete;'.format(c,c)
    print '{} &operator=(const {}&&) = delete;'.format(c,c)
else:
    print 'Error: provide class name as program argument'

