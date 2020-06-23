Name:     ssa-daemon
Version:  1.0
Release:  1%{?dist} #Fixme
Summary:  The SSA daemon from the Usable Security and Privacy Lab
License:  LGPLv3 #can add license file
URL:      https://github.com/Usable-Security-and-Privacy-Lab/ssa-daemon/
Source0:  https://github.com/Usable-Security-and-Privacy-Lab/ssa-daemon/-%{1.0}.tar.gz

BuildArch:     x86_64 #noarch?
BuildRequires: gettext ssa # make kmod package

Requires(post): info
Requires(preun): info

%description
The SSA is a system that allows programmers to easily create secure TLS
connections using the standard POSIX socket API. This allows programmers
to focus more on the development of their apps without having to interface
with complicated TLS libraries. The SSA also allows system administrators and
other power users to customize TLS settings for all connections on the machines
they manage, according to their own needs.

%prep
%autosetup

%build #fixme
%configure #Fixme
make

%install #Fixme
mkdir -p %{buildroot}%{_bindir}
install -p -m 755 %{SOURCE0} %{buildroot}%{_bindir}/ssa-daemon.sh
%make_install //Fixme
%find_lang %{name}
rm

%post #?
%preun

%files #Fixme
%{_bindir}/ssa-daemon/* #/usr/bin
%doc
%license LICENSE
%changelog

%doc %{_mandir}/docs/*

%install

#%_prefix            /usr
   %_exec_prefix       %{_prefix}
   %_bindir            %{_exec_prefix}/bin
   %_sbindir           %{_exec_prefix}/sbin
   %_libexecdir        %{_exec_prefix}/libexec
   %_datadir           %{_prefix}/share
   %_sysconfdir        %{_prefix}/etc
   %_sharedstatedir    %{_prefix}/com
   %_localstatedir     %{_prefix}/var
   %_libdir            %{_exec_prefix}/lib
   %_includedir        %{_prefix}/include
   %_oldincludedir     /usr/include
   %_infodir           %{_prefix}/info
   %_mandir            %{_prefix}/man
