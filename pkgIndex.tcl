#
# Tcl package index file
#
package ifneeded cubrid 0.9.6 \
    [list load [file join $dir libcubrid0.9.6.so] cubrid]

package ifneeded tdbc::cubrid 0.9.6 \
    [list source [file join $dir tdbccubrid.tcl]]
