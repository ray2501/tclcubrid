%{!?directory:%define directory /usr}

%define buildroot %{_tmppath}/%{name}

Name:          tclcubrid
Summary:       CUBRID CCI Client Interface wrapper for Tcl
Version:       0.9.5
Release:       1
License:       MIT
Group:         Development/Libraries/Tcl
Source:        https://github.com/ray2501/tclcubrid/tclcubrid_0.9.5.zip
URL:           https://github.com/ray2501/tclcubrid
BuildRequires: autoconf
BuildRequires: make
BuildRequires: tcl-devel >= 8.6
Requires:      tcl >= 8.6
BuildRoot:     %{buildroot}

%description
tclcubrid is a Tcl extension by using CUBRID CCI Client Interface to
connect CUBRID database.

This extension is using Tcl_LoadFile to load CCI library.

%prep
%setup -q -n %{name}

%build
CFLAGS="%optflags" ./configure \
	--prefix=%{directory} \
	--exec-prefix=%{directory} \
	--libdir=%{directory}/%{_lib}
make 

%install
make DESTDIR=%{buildroot} pkglibdir=%{directory}/%{_lib}/tcl/%{name}%{version} install

%clean
rm -rf %buildroot

%files
%defattr(-,root,root)
%{directory}/%{_lib}/tcl
