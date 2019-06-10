#!/usr/bin/env python3

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#!/usr/bin/python3
##
# @file
# @brief File to create string arrays
#
# This file is included to assist the developer using the API in creating the arrays for the username, password, topic name, and client ID strings. It prints both the array itself as well as a variable with the length of the string. These can then be passed to os_memcpy() to copy the information into the mqtt_session_t.

###############################################
# First argument is the value to be encoded,  #
# second argument is the name of the variable #
# to be used.                                 #
###############################################

import sys
import os

inString = str(sys.argv[1]);

print("static const char {0}[{1}] = {{ ".format(sys.argv[2], len(inString)), end='')
for letter in inString[:-1]:
    p = ord(letter)
    print("{0:#x}, ".format(p), end='')

p = ord(inString[-1])
print("{0:#x} ".format(int(p)), end='')
print("}; // " + inString)
print("static const uint8_t {0}_len = {1};".format(sys.argv[2], len(inString)))