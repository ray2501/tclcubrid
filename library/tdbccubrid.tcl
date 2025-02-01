#------------------------------------------------------------------------------
# tdbccubrid.tcl --
#
#      Tcl DataBase Connectivity CUBRID Driver
#      Class definitions and Tcl-level methods for the tdbc::cubrid bridge.
#
#------------------------------------------------------------------------------

package require tdbc
package require cubrid

package provide tdbc::cubrid 0.10.0


::namespace eval ::tdbc::cubrid {

    namespace export connection

}


#------------------------------------------------------------------------------
#
# tdbc::cubrid::connection --
#
#	Class representing a connection to a CUBRID database.
#
#-------------------------------------------------------------------------------

::oo::class create ::tdbc::cubrid::connection {

    superclass ::tdbc::connection

    constructor {host port databaseName username password {property ""} {args ""}} {
        next

        if {[llength $args] % 2 != 0} {
            set cmd [lrange [info level 0] 0 end-[llength $args]]
            return -code error \
            -errorcode {TDBC GENERAL_ERROR HY000 CUBRID WRONGNUMARGS} \
            "wrong # args, should be \"$cmd ?-option value?...\""
        }

        if {[string length $property] > 0} {
            if {[catch {cubrid [namespace current]::DB -host $host \
                      -port $port -dbname $databaseName \
                      -user $username -passwd $password \
                      -property $property } errorMsg]} {
                error $errorMsg
            }
        } else {
            if {[catch {cubrid [namespace current]::DB -host $host \
                      -port $port -dbname $databaseName \
                      -user $username -passwd $password } errorMsg]} {
                error $errorMsg
            }
        }


        if {[llength $args] > 0} {
            my configure {*}$args
        }

        # default needs ON
        [namespace current]::DB setAutocommit 1
    }

    forward statementCreate ::tdbc::cubrid::statement create

    method configure args {
        if {[llength $args] == 0} {
            set result -isolation
            lappend result [ [namespace current]::DB getIsolationLevel ]
            lappend result -readonly 0
            return $result
        } elseif {[llength $args] == 1} {
            set option [lindex $args 0]
            switch -exact -- $option {
                -i - -is - -iso - -isol - -isola - -isolat - -isolati -
                -isolatio - -isolation {
                    return [ [namespace current]::DB getIsolationLevel ]
                }
                -r - -re - -rea - -read - -reado - -readon - -readonl -
		-readonly {
		    return 0
		}
                default {
                    return -code error \
                    -errorcode [list TDBC GENERAL_ERROR HY000 CUBRID \
                            BADOPTION $option] \
                    "bad option \"$option\": must be -isolation or -readonly"
                }
            }
        } elseif {[llength $args] % 2 != 0} {
            set cmd [lrange [info level 0] 0 end-[llength $args]]
            return -code error \
            -errorcode [list TDBC GENERAL_ERROR HY000 \
                    CUBRID WRONGNUMARGS] \
            "wrong # args, should be \" $cmd ?-option value?...\""
        }

        foreach {option value} $args {
            switch -exact -- $option {
                -i - -is - -iso - -isol - -isola - -isolat - -isolati -
                -isolatio - -isolation {
                    switch -exact -- $value {
                    readc - readco - readcom - readcomm - readcommi -
                    readcommit - readcommitt - readcommitte -
                    readcommitted {
                       [namespace current]::DB setIsolationLevel readcommitted
                    }
                    rep - repe - repea - repeat - repeata - repeatab -
                    repeatabl - repeatable - repeatabler - repeatablere -
                    repeatablerea - repeatablread {
                       [namespace current]::DB setIsolationLevel repeatablread
                    }
                    s - se - ser - seri - seria - serial - seriali -
                    serializ - serializa - serializab - serializabl -
                    serializable {
                       [namespace current]::DB setIsolationLevel serializable
                    }
                    default {
                        return -code error \
                        -errorcode [list TDBC GENERAL_ERROR HY000 \
                                CUBRID BADISOLATION $value] \
                        "bad isolation level \"$value\":\
                                        should be readcommitted, repeatableread \
                                        or serializable"
                    }
                    }
                }
		-r - -re - -rea - -read - -reado - -readon - -readonl -
		-readonly {
		    if {$value} {
			return -code error \
			    -errorcode [list TDBC FEATURE_NOT_SUPPORTED 0A000 \
					    CUBRID READONLY] \
			    "-readonly not supported to setup."
		    }
		}
                default {
                    return -code error \
                    -errorcode [list TDBC GENERAL_ERROR HY000 \
                            CUBRID BADOPTION $value] \
                    "bad option \"$option\": must be -isolation or -readonly"
                }
            }
        }
        return
    }

    # invoke close method -> destroy our object
    method close {} {
        set mystats [my statements]
        foreach mystat $mystats {
            $mystat close
        }
        unset mystats

        [namespace current]::DB close
        next
    }

    method tables {{pattern %}} {
        set retval {}

        # I only know how to get user table name list
        set stmt [[namespace current]::DB prepare "SELECT class_name name \
                   FROM db_class WHERE class_name like '$pattern' AND \
                   is_system_class = 'NO'"]

        $stmt execute
        while {[$stmt cursor 1 CURRENT] != 0} {
            set row [$stmt fetch_row_dict]
            dict set row name [string tolower [dict get $row name]]
            dict set retval [dict get $row name] $row
        }
        $stmt close

        return $retval
    }

    method columns {table {pattern %}} {
        set retval {}

        # Setup our pattern
        set pattern [string map [list \
                                     * {[*]} \
                                     ? {[?]} \
                                     \[ \\\[ \
                                     \] \\\[ \
                                     _ ? \
                                     % *] [string tolower $pattern]]

        # I only know how to get column name list
        set stmt [[namespace current]::DB prepare "SELECT a.attr_name \
                   name FROM db_attribute as a WHERE \
                   class_name = '$table' ORDER BY def_order"]

        $stmt execute

        while {[$stmt cursor 1 CURRENT] != 0} {
            set row [$stmt fetch_row_dict]
            dict set row name [string tolower [dict get $row name]]

            set column_name [dict get $row name]
            if {![string match $pattern $column_name]} {
                continue
            }

            dict set retval [dict get $row name] $row
        }
        $stmt close

        return $retval
    }

    method primarykeys {table} {
        set retval {}
        set stmt [[namespace current]::DB prepare "SELECT index_name name FROM \
            db_index WHERE class_name = '$table' AND is_primary_key = 'YES'"]
        $stmt execute

        set retval [dict create]

        # Add table name
        dict set retval tableName $table
        while {[$stmt cursor 1 CURRENT] != 0} {
            set row [$stmt fetch_row_dict]
            dict set row name [string tolower [dict get $row name]]

            # Get key name
            set key_name [dict get $row name]
            dict set retval keyName $key_name
        }
        $stmt close

        return $retval
    }

    method foreignkeys {args} {
        set length [llength $args]
        set retval {}
        set ftable ""

        if { $length != 2 || $length%2 != 0} {
            return -code error \
            -errorcode [list TDBC GENERAL_ERROR HY000 \
                    CUBRID WRONGNUMARGS] \
            "wrong # args: should be \
             [lrange [info level 0] 0 1] -foreign tableName"

            return $retval
        }

        # I only know how to list foreign keys of a table, maybe not correct
        foreach {option table} $args {
            if {[string compare $option "-foreign"]==0} {
                set ftable $table
            } else {
                return $retval
            }
        }

        set sql "SELECT index_name name FROM db_index WHERE \
                  class_name = '$ftable' AND is_foreign_key = 'YES'"
        set stmt [[namespace current]::DB prepare $sql]
        $stmt execute

        set retval [dict create]
        set vallist [list]

        # Add table name
        dict set retval tableName $ftable
        while {[$stmt cursor 1 CURRENT] != 0} {
            set row [$stmt fetch_row_dict]
            dict set row name [string tolower [dict get $row name]]

            # Get key name
            set key_name [dict get $row name]
            lappend vallist $key_name
        }

        if {[llength $vallist] != 0} {
            dict set retval keyName $vallist
        }
        $stmt close

        return $retval
    }

    # The 'prepareCall' method gives a portable interface to prepare
    # calls to stored procedures.  It delegates to 'prepare' to do the
    # actual work.
    method preparecall {call} {
        regexp {^[[:space:]]*(?:([A-Za-z_][A-Za-z_0-9]*)[[:space:]]*=)?(.*)} \
            $call -> varName rest
        if {$varName eq {}} {
            my prepare \\{$rest\\}
        } else {
            my prepare \\{:$varName=$rest\\}
        }
    }

    # The 'begintransaction' method launches a database transaction
    method begintransaction {} {
        [namespace current]::DB setAutocommit 0
    }

    # The 'commit' method commits a database transaction
    method commit {} {
        [namespace current]::DB commit
        [namespace current]::DB setAutocommit 1
    }

    # The 'rollback' method abandons a database transaction
    method rollback {} {
        [namespace current]::DB rollback
        [namespace current]::DB setAutocommit 1
    }

    method prepare {sqlCode} {
        set result [next $sqlCode]
        return $result
    }

    method getDBhandle {} {
        return [namespace current]::DB
    }

}


#------------------------------------------------------------------------------
#
# tdbc::cubrid::statement --
#
#	The class 'tdbc::cubrid::statement' models one statement against a
#       database accessed through a CUBRID connection
#
#------------------------------------------------------------------------------

::oo::class create ::tdbc::cubrid::statement {

    superclass ::tdbc::statement

    variable Params db sql stmt

    constructor {connection sqlcode} {
        next
        set Params {}
        set db [$connection getDBhandle]
        set sql {}
        foreach token [::tdbc::tokenize $sqlcode] {

            # I have no idea how to get params meta here,
            # just give a default value.
            if {[string index $token 0] in {$ : @}} {
                dict set Params [string range $token 1 end] \
                    {type varchar direction in}

                append sql "?"
                continue
            }

            append sql $token
        }

        set stmt [$db prepare $sql]
    }

    forward resultSetCreate ::tdbc::cubrid::resultset create

    method close {} {
        set mysets [my resultsets]
        foreach myset $mysets {
            $myset close
        }
        unset mysets

        $stmt close

        next
    }

    # The 'params' method returns descriptions of the parameters accepted
    # by the statement
    method params {} {
        return $Params
    }

    method paramtype args {
        set length [llength $args]

        if {$length < 2} {
            set cmd [lrange [info level 0] 0 end-[llength $args]]
            return -code error \
            -errorcode {TDBC GENERAL_ERROR HY000 CUBRID WRONGNUMARGS} \
            "wrong # args...\""
        }

        set parameter [lindex $args 0]
        if { [catch  {set value [dict get $Params $parameter]}] } {
            set cmd [lrange [info level 0] 0 end-[llength $args]]
            return -code error \
            -errorcode {TDBC GENERAL_ERROR HY000 CUBRID BADOPTION} \
            "wrong param...\""
        }

        set count 1
        if {$length > 1} {
            set direction [lindex $args $count]

            if {$direction in {in out inout}} {
                # I don't know how to setup direction, setup to in
                dict set value direction in
                incr count 1
            }
        }

        if {$length > $count} {
            set type [lindex $args $count]

            # Only accept these types
            if {$type in {char varchar bit varbit numeric integer smallint \
                          real float double monetary date time timestamp \
                          timestamptz timestampltz bigint datetime datetimetz \
                          datetimeltz clob blob set multiset sequence \
                          enum json null}} {
                dict set value type $type
            }
        }

        # Skip other parameters and setup
        dict set Params $parameter $value
    }

    method getStmthandle {} {
        return $stmt
    }

    method getSql {} {
        return $sql
    }

    method getRowCount {} {
       return [$db row_count]
    }

}


#------------------------------------------------------------------------------
#
# tdbc::cubrid::resultset --
#
#	The class 'tdbc::cubrid::resultset' models the result set that is
#	produced by executing a statement against a CUBRID database.
#
#------------------------------------------------------------------------------

::oo::class create ::tdbc::cubrid::resultset {

    superclass ::tdbc::resultset

    variable -set {*}{
        -stmt -sql -results -params -RowCount -columns
    }

    constructor {statement args} {
        next
    	set -stmt [$statement getStmthandle]
        set -params  [$statement params]
        set -sql [$statement getSql]
        set -results {}
        set -RowCount 0

        if {[llength $args] == 0} {
            set keylist [dict keys ${-params}]
            set count 1

            foreach mykey $keylist {

                if {[info exists ::$mykey] == 1} {
                    upvar 1 $mykey mykey1
                    set type [dict get [dict get ${-params} $mykey] type]

                    catch {${-stmt} bind $count $type $mykey1}
                }

                incr count 1
            }
            ${-stmt} execute
            set -RowCount [$statement getRowCount]
        } elseif {[llength $args] == 1} {
            # If the dict parameter is supplied, it is searched for a key
            # whose name matches the name of the bound variable
            set -paramDict [lindex $args 0]

            set keylist [dict keys ${-params}]
            set count 1

            foreach mykey $keylist {

                if {[catch {set bound [dict get ${-paramDict} $mykey]}]==0} {
                    set type [dict get [dict get ${-params} $mykey] type]

                    catch {${-stmt} bind $count $type $bound}
                }

                incr count 1
            }
            ${-stmt} execute
            set -RowCount [$statement getRowCount]
        } else {
            return -code error \
            -errorcode [list TDBC GENERAL_ERROR HY000 \
                    CUBRID WRONGNUMARGS] \
            "wrong # args: should be\
                     [lrange [info level 0] 0 1] statement ?dictionary?"
        }
    }

    # Return a list of the columns
    method columns {} {
        set -columns [ ${-stmt} columns ]
        return ${-columns}
    }

    method nextresults {} {
        set have 0

        # Is it really OK? check current cursor status
        if {[catch {set have [${-stmt} cursor 0 CURRENT]}]} {
	    return 0
        }

        return $have
    }

    method nextlist var {
        upvar 1 $var row
        set row {}
        set result 0

        variable mylist

        if {[catch {set result [${-stmt} cursor 1 CURRENT]}]} {
	    return 0
        }

        if {$result == 0} {
            return 0
        }

        if { [catch {set mylist [ ${-stmt} fetch_row_list ]}] } {
            return 0
        }

        if {[llength $mylist] == 0} {
            return 0
        }

        set row $mylist
        return 1
    }

    method nextdict var {
        upvar 1 $var row
        set row {}
        set result 0

        variable mydict

        if {[catch {set result [${-stmt} cursor 1 CURRENT]}]} {
	    return 0
        }

        if {$result == 0} {
            return 0
        }

        if { [catch {set mydict [ ${-stmt} fetch_row_dict ]}] } {
            return 0
        }

        if {[dict size $mydict] == 0} {
            return 0
        }

        set row $mydict
        return 1
    }

    # Return the number of rows affected by a statement
    method rowcount {} {
        return ${-RowCount}
    }

}
