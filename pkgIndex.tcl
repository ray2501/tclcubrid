# -*- tcl -*-
# Tcl package index file, version 1.1
#
if {[package vsatisfies [package provide Tcl] 9.0-]} {
    package ifneeded cubrid 0.9.6 \
	    [list load [file join $dir libtcl9cubrid0.9.6.so] [string totitle cubrid]]
} else {
    package ifneeded cubrid 0.9.6 \
	    [list load [file join $dir libcubrid0.9.6.so] [string totitle cubrid]]
}

package ifneeded tdbc::cubrid 0.9.6 \
    [list source [file join $dir tdbccubrid.tcl]]
