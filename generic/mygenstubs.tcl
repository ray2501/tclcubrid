#!/usr/bin/tclsh

set variable [list tclsh ../tools/genExtStubs.tcl cubridStubDefs.txt cubridStubs.h cubridStubInit.c]
exec {*}$variable
