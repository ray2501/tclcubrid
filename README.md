tclcubrid
=====

[CUBRID](http://www.cubrid.org/) is an open source SQL-based
relational database management system with object extensions
developed by
[Naver Corporation](https://en.wikipedia.org/wiki/Naver_Corporation) for web applications.

CUBRID is an object-relational database management system (DBMS)
consisting of the database server, the broker, and the CUBRID Manager.

 * As the core component of the CUBRID database management system,
   the database server stores and manages data in multi-threaded
   client/server architecture.
 * The broker is a CUBRID-specific middleware that relays the communication
   between the database server and external applications.
 * The CUBRID Manager is a GUI tool that allows users to remotely manage
   the database and the broker.

tclcubrid is a Tcl extension by using CUBRID
[CCI (CCI Client Interface)](http://cubrid.org/manual/en/10.0/api/cci.html)
driver to connect CUBRID. CUBRID CCI driver is connected through the CUBRID
broker. I write this extension to research CUBRID and CCI (CCI Client Interface)
driver.

[Tcl Database Connectivity (TDBC)](http://www.tcl.tk/man/tcl8.6/TdbcCmd/tdbc.htm)
is a common interface for Tcl programs to access SQL databases.
Tclcubrid's TDBC interface (tdbc::cubrid) is based on tclcubrid extension.
I write Tclcubrid's TDBC interface to study TDBC interface.

This extension is using [Tcl_LoadFile](https://www.tcl.tk/man/tcl/TclLib/Load.htm) to
load CCI (CCI Client Interface) driver. Before using this extension,
please setup libcascci path environment variable.
Below is an example on Windows platform:

    set PATH=C:\CUBRID\bin;C:\CUBRID\lib;%PATH%

Below is an example on Linux platform to setup LD_LIBRARY_PATH environment variable:

    export CUBRID=/home/danilo/Programs/CUBRID
    export LD_LIBRARY_PATH=$CUBRID/lib:$LD_LIBRARY_PATH

This extension needs Tcl 8.6.

Related Extension
=====

Tcl users can use [TDBC-ODBC](https://www.tcl.tk/man/tcl/TdbcodbcCmd/tdbc_odbc.htm) bridge via CUBRID
[ODBC driver](http://cubrid.org/manual/en/10.0/api/odbc.html) to connect CUBRID database.

Or you can try my other project [TDBCJDBC](https://github.com/ray2501/TDBCJDBC) via CUBRID
[JDBC driver](http://cubrid.org/manual/en/10.0/api/jdbc.html) to connect CUBRID database.


License
=====

CUBRID has a separate license for its server engine and its interfaces.
The server engine adopts the GPL v2.0 or later license, which allows distribution,
modification, and acquisition of the source code.
CUBRID APIs and GUI tools have the Berkeley Software Distribution license in which
there is no obligation of opening derivative works.

tclcubrid is Licensed under MIT license.


UNIX BUILD
=====

Building under most UNIX systems is easy, just run the configure script and
then run make. For more information about the build process, see the
tcl/unix/README file in the Tcl src dist. The following minimal example will
install the extension in the /opt/tcl directory.

    $ cd tclcubrid
    $ ./configure --prefix=/opt/tcl
    $ make
    $ make install

If you need setup directory containing tcl configuration (tclConfig.sh),
below is an example:

    $ cd tclcubrid
    $ ./configure --with-tcl=/opt/activetcl/lib
    $ make
    $ make install


WINDOWS BUILD
=====

The recommended method to build extensions under windows is to use the
Msys + Mingw build process. This provides a Unix-style build while generating
native Windows binaries. Using the Msys + Mingw build tools means that you
can use the same configure script as per the Unix build to create a Makefile.


Implement commands
=====

The interface to the CUBRID CCI (CCI Client Interface) driver consists of 
single tcl command named `cubrid`. Once a CUBRID database connection is created,
it can be controlled using methods of the HANDLE command.

cubrid HANDLE ?-host HOST? ?-port PORT? ?-dbname DBNAME? ?-user username? ?-passwd password? ?-property properties?  
HANDLE getAutocommit  
HANDLE setAutocommit autocommit  
HANDLE getIsolationLevel  
HANDLE setIsolationLevel level  
HANDLE commit  
HANDLE rollback  
HANDLE prepare SQL_String  
HANDLE server_version   
HANDLE row_count  
HANDLE last_insert_id  
HANDLE close  
STMT_HANDLE bind index type value  
STMT_HANDLE execute  
STMT_HANDLE cursor offset pos  
STMT_HANDLE fetch_row_list  
STMT_HANDLE fetch_row_dict  
STMT_HANDLE columns  
STMT_HANDLE columntype index   
STMT_HANDLE close  

`cubrid` command options are used to make connection to CUBRID.
Below is the option default value (if user does not specify):

| Option            | Type      | Default                         | Additional description |
| :---------------- | :-------- | :------------------------------ | :--------------------- |
| host              | string    | localhost                       |
| port              | integer   | 33000                           |
| dbname            | string    | demo                            | Just for my environment
| user              | string    | public                          |
| passwd            | string    |                                 |
| property          | string    |                                 |

The default value of auto-commit mode can be configured by using
CCI_DEFAULT_AUTOCOMMIT which is a broker parameter.
If it is omitted, the default value is set to ON.
User can use `setAutocommit` command to switch autocommit mode off.

setIsolationLevel method configures the isolation level.
Supported value: readcommitted, repeatableread, serializable

STMT_HANDLE bind type supported value:
char, varchar, bit, varbit, numeric, integer, smallint, real, float, double,
monetary, date, time, timestamp, timestamptz, timestampltz, bigint, datetime,
datetimetz, datetimeltz, clob, blob, set, multiset, sequence, enum, null

(note: In CUBRID database FLOAT and REAL are used interchangeably.)

clob and blob is an experiment function, both has size limitation
(now is 1048576 for read/write).

SET is a collection type in which each element has different values.
Elements of a SET are allowed to have only one data type. Now only support
string data type.

LIST (= SEQUENCE) is a collection type in which the input order of elements
is preserved, and duplications are allowed. Elements of a LIST are allowed
to have only one data type. Now only support string data type.

STMT_HANDLE cursor pos supported value:
FIRST, CURRENT, LAST

## TDBC commands

tdbc::cubrid::connection create db host port dbname username password property ?-option value...?

Connection to a CUBRID database is established by invoking `tdbc::cubrid::connection create`,
passing it the name to be used as a connection handle,
followed by a host name, port number, dbname, username and password.

The tdbc::cubrid::connection create object command supports the -isolation option.
Supported value: readcommitted, repeatableread, serializable.

CUBRID driver for TDBC implements a statement object that represents a SQL statement in a database.
Instances of this object are created by executing the `prepare` or `preparecall` object
command on a database connection.

The `prepare` object command against the connection accepts arbitrary SQL code
to be executed against the database.

The `paramtype` object command allows the script to specify the type and direction of parameter
transmission of a variable in a statement.
Now CUBRID driver only specify the type work.

CUBRID driver paramtype accepts below type (follow tclcubrid support type):
char, varchar, bit, varbit, numeric, integer, smallint, real, float, double,
monetary, date, time, timestamp, timestamptz, timestampltz, bigint, datetime,
datetimetz, datetimeltz, clob, blob, set, multiset, sequence, enum, null

The `execute` object command executes the statement.


Example
=====

I test below examples at version 10.0.0 (1376).

## tclcubrid example

Get version

    package require cubrid
    cubrid db -host localhost -port 33000 -dbname demo -user danilo -passwd danilo
    db server_version
    db close

List tables -

    package require cubrid
    cubrid db -host localhost -port 33000 -dbname demo -user danilo -passwd danilo
    set stmt [db prepare {show tables}]
    $stmt execute
    
    while {[$stmt cursor 1 CURRENT] != 0} {
        puts [$stmt fetch_row_dict]
    }
    
    $stmt close
    db close

A simple example:

    package require cubrid
    cubrid db -host localhost -port 33000 -dbname demo -user danilo -passwd danilo

    set stmt [db prepare \
        {create table IF NOT EXISTS power (id bigint AUTO_INCREMENT PRIMARY KEY, 
         name varchar(40), number double)}]
    $stmt execute
    $stmt close

    set stmt [db prepare {insert into power (name, number) values(?, ?)}]
    $stmt bind 1 varchar Danilo
    $stmt bind 2 double 100.01
    $stmt execute
    $stmt bind 1 varchar Smith
    $stmt bind 2 double 10.01
    $stmt execute
    $stmt close

    set stmt [db prepare {select * from power}]
    $stmt execute

    while {[$stmt cursor 1 CURRENT] != 0} {
        puts [$stmt fetch_row_dict]
    }

    $stmt close

    set stmt [db prepare {DROP TABLE IF EXISTS power}]
    $stmt execute
    $stmt close

    db close

A simple example for bit type:

    package require cubrid
    cubrid db -host localhost -port 33000 -dbname demo -user danilo -passwd danilo

    set stmt [db prepare \
        {CREATE TABLE IF NOT EXISTS bittest (id smallint PRIMARY KEY, str bit(8))}]
    $stmt execute
    $stmt close

    set stmt [db prepare {INSERT INTO bittest VALUES (1, B'00101000')}]
    $stmt execute
    $stmt close

    set stmt [db prepare {INSERT INTO bittest VALUES (?, ?)}]
    $stmt bind 1 smallint 2
    $stmt bind 2 bit "11010111"
    $stmt execute
    $stmt close

    set stmt [db prepare {select * from bittest}]
    $stmt execute

    while {[$stmt cursor 1 CURRENT] != 0} {
        set row [$stmt fetch_row_dict]
        puts [dict get $row id]

        #Decode our bit data
        binary scan [dict get $row str] B* var1
        puts $var1
    }

    $stmt close

    set stmt [db prepare {DROP TABLE IF EXISTS bittest}]
    $stmt execute
    $stmt close

    db close

A simple example for set type:

    package require cubrid
    cubrid db -host localhost -port 33000 -dbname demo -user danilo -passwd danilo

    set stmt [db prepare \
        {CREATE TABLE IF NOT EXISTS set_tbl (col_1 SET (CHAR(1)))}]
    $stmt execute
    $stmt close

    set stmt [db prepare {INSERT INTO set_tbl VALUES ({'c','c','c','b','b', 'a'})}]
    $stmt execute
    $stmt close

    set stmt [db prepare {INSERT INTO set_tbl VALUES (?)}]
    set sdata [list d e f]
    $stmt bind 1 set $sdata
    $stmt execute
    $stmt close

    set stmt [db prepare {select * from set_tbl}]
    $stmt execute
    puts [$stmt columntype 1]
    while {[$stmt cursor 1 CURRENT] != 0} {
        set row [$stmt fetch_row_dict]
        puts [dict get $row col_1]
    }
    $stmt close

    set stmt [db prepare {DROP TABLE IF EXISTS set_tbl}]
    $stmt execute
    $stmt close

    db close

## Tcl Database Connectivity (TDBC) interface

Below is an exmaple:

    package require tdbc::cubrid

    tdbc::cubrid::connection create db localhost 33000 demo danilo danilo

    set statement [db prepare \
        {create table if not exists person (id integer primary key, name varchar(40) not null)}]
    $statement execute
    $statement close

    set statement [db prepare {insert into person values(1, 'leo')}]
    $statement execute
    $statement close

    set statement [db prepare {insert into person values(2, 'yui')}]
    $statement execute
    $statement close

    set statement [db prepare {insert into person values(:id, :name)}]
    $statement paramtype id integer
    $statement paramtype name varchar

    set id 3
    set name danilo
    $statement execute

    set myparams [dict create id 4 name arthur]
    $statement execute $myparams
    $statement close

    set statement [db prepare {SELECT * FROM person}]

    $statement foreach row {
        puts [dict get $row id]
        puts [dict get $row name]
    }

    $statement close

    set statement [db prepare {drop table if exists person}]
    $statement execute
    $statement close

    db close

Another example:

    package require tdbc::cubrid

    tdbc::cubrid::connection create db localhost 33000 demo danilo danilo \
                             "login_timeout=600" -isolation readcommitted

    set statement [db prepare \
        {create table contact (name varchar(20) not null  UNIQUE, 
        email varchar(40) not null, primary key(name))}]
    $statement execute
    $statement close

    set statement [db prepare {insert into contact values(:name, :email)}]
    $statement paramtype name varchar
    $statement paramtype email varchar

    set name danilo
    set email danilo@test.com
    $statement execute

    set name scott
    set email scott@test.com
    $statement execute

    set myparams [dict create name arthur email arthur@example.com]
    $statement execute $myparams
    $statement close

    set statement [db prepare {SELECT * FROM contact}]
    $statement foreach row {
        puts [dict get $row name]
        puts [dict get $row email]
    }
    $statement close

    set statement [db prepare {DROP TABLE  contact}]
    $statement execute
    $statement close
    db close

