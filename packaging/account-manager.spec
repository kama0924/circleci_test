Name:       account-manager
Summary:    Account Manager
Version:    0.1.18
Release:    1
Group:      Social & Content/Other
License:    Apache-2.0
Source0:    account-manager-%{version}.tar.gz
Source1:    org.tizen.account.manager.service
Source2:    org.tizen.account.manager.conf
Source3:    accounts-service.service

BuildRequires:  cmake
BuildRequires:  pkgconfig(dlog)
BuildRequires:  pkgconfig(db-util)
BuildRequires:  pkgconfig(capi-base-common)
BuildRequires:  pkgconfig(capi-system-info)
BuildRequires:  pkgconfig(pkgmgr-info)
BuildRequires:	pkgconfig(glib-2.0) >= 2.26
BuildRequires:  pkgconfig(gio-2.0)
BuildRequires:  pkgconfig(vconf)
BuildRequires:  pkgconfig(cynara-client)
BuildRequires:  pkgconfig(cynara-session)
BuildRequires:  pkgconfig(cynara-creds-gdbus)
BuildRequires:  pkgconfig(account-common)
BuildRequires:  pkgconfig(libtzplatform-config)

Requires(post): /sbin/ldconfig
Requires(post): /usr/bin/sqlite3
Requires(postun): /sbin/ldconfig


%description
Account Daemon: no

%prep
%setup -q
cp %{SOURCE1} .

%build
#export   CFLAGS+=" -Wextra -Wcast-align -Wcast-qual -Wshadow -Wwrite-strings -Wswitch-default"
#export CXXFLAGS+=" -Wextra -Wcast-align -Wcast-qual -Wshadow -Wwrite-strings -Wswitch-default -Wnon-virtual-dtor -Wno-c++0x-compat"
#export   CFLAGS+=" -Wno-unused-parameter -Wno-empty-body"
#export CXXFLAGS+=" -Wno-unused-parameter -Wno-empty-body"

#export   CFLAGS+=" -fno-omit-frame-pointer -fno-optimize-sibling-calls -fno-strict-aliasing -fno-unroll-loops -fsigned-char -fstrict-overflow -fno-common"
#export CXXFLAGS+=" -fno-omit-frame-pointer -fno-optimize-sibling-calls -fno-strict-aliasing -fno-unroll-loops -fsigned-char -fstrict-overflow"

export CFLAGS="${CFLAGS} -fvisibility=hidden -fPIE"
export LDFLAGS="${LDFLAGS} -pie"
cmake . -DCMAKE_INSTALL_PREFIX=/usr -DLIBDIR=%{_libdir}

make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install

rm -rf %{buildroot}/usr/lib/account-manager

mkdir -p %{buildroot}/usr/share/dbus-1/system-services
install -m 0644 %SOURCE1 %{buildroot}/usr/share/dbus-1/system-services/org.tizen.account.manager.service

mkdir -p %{buildroot}%{_sysconfdir}/dbus-1/system.d
install -m 0644 %{SOURCE2} %{buildroot}%{_sysconfdir}/dbus-1/system.d/

mkdir -p %{buildroot}%{_unitdir}/multi-user.target.wants
install -m 644 %SOURCE3 %{buildroot}%{_unitdir}/accounts-service.service
%install_service multi-user.target.wants accounts-service.service

%post
/sbin/ldconfig
#if [ ! -d /opt/usr/dbspace ]
#then
#        mkdir -p /opt/usr/dbspace
#fi
mkdir -p %{TZ_SYS_DB}
if [ ! -f %{TZ_SYS_DB}/.account.db ]
then
        sqlite3 %{TZ_SYS_DB}/.account.db 'PRAGMA journal_mode = PERSIST;
        CREATE TABLE if not exists label (AppId TEXT, Label TEXT, Locale TEXT);
        CREATE TABLE if not exists account_type (_id INTEGER PRIMARY KEY AUTOINCREMENT, AppId TEXT,
        ServiceProviderId TEXT, IconPath TEXT, SmallIconPath TEXT, MultipleAccountSupport INT);
        CREATE TABLE if not exists account_custom (AccountId INTEGER, AppId TEXT, Key TEXT, Value TEXT);
        CREATE TABLE if not exists account (_id INTEGER PRIMARY KEY AUTOINCREMENT, user_name TEXT, email_address TEXT, display_name TEXT, icon_path TEXT,
        source TEXT, package_name TEXT, access_token TEXT, domain_name TEXT, auth_type INTEGER, secret INTEGER, sync_support INTEGER,
        txt_custom0 TEXT, txt_custom1 TEXT, txt_custom2 TEXT, txt_custom3 TEXT, txt_custom4 TEXT,
        int_custom0 INTEGER, int_custom1 INTEGER, int_custom2 INTEGER, int_custom3 INTEGER, int_custom4 INTEGER);
        CREATE TABLE if not exists capability (_id INTEGER PRIMARY KEY AUTOINCREMENT, key TEXT, value INTEGER,
	package_name TEXT, user_name TEXT,  account_id INTEGER, FOREIGN KEY (account_id) REFERENCES account(_id));
	CREATE TABLE if not exists provider_feature (app_id TEXT, key TEXT);
'
fi

chown service_fw:service_fw %{TZ_SYS_DB}/.account.db
chown service_fw:service_fw %{TZ_SYS_DB}/.account.db-journal

chmod 644 %{TZ_SYS_DB}/.account.db
chmod 644 %{TZ_SYS_DB}/.account.db-journal

#smack labeling
#chsmack -a 'System::Shared' %{TZ_SYS_DB}/.account.db-journal
#chsmack -a 'System::Shared' %{TZ_SYS_DB}/.account.db
%postun -p /sbin/ldconfig


%files
%manifest account-svcd.manifest
#%defattr(-,system,system,-)
%config %{_sysconfdir}/dbus-1/system.d/org.tizen.account.manager.conf
%license LICENSE.APLv2
%{_bindir}/account-svcd
%attr(0644,root,root) %{_unitdir}/accounts-service.service
%attr(0644,root,root) %{_unitdir}/multi-user.target.wants/accounts-service.service
%attr(0644,root,root) /usr/share/dbus-1/system-services/org.tizen.account.manager.service

