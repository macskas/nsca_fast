#!/bin/bash -e

PACKAGE_NAME=${PACKAGE_NAME:=nsca-fast}
PACKAGE_VERSION=${PACKAGE_VERSION:=2.9.1-3}
USERNAME=${GITHUB_ACTOR:=macskas}
CHDATE=$(date +%a", "%d" "%b" "%Y" "%H:%M:%S" "%z)
DEBIAN_CODENAME=$(lsb_release -c -s)
DEBIAN_ID=$(lsb_release -i -s|tr '[:upper:]' '[:lower:]')
DIR_DEBIAN=".github/.debian"
MY_RUNNER_NAME="$DEBIAN_ID-$DEBIAN_CODENAME"
ARCH="amd64"
RELEASE_PREFIX="${PACKAGE_NAME}_${PACKAGE_VERSION}_${MY_RUNNER_NAME}_$ARCH"

do_changelog()
{
cat << EOF > debian/changelog
$PACKAGE_NAME ($PACKAGE_VERSION) unstable; urgency=low

  * Just a release

 -- $USERNAME <$USERNAME@email.fake>  $CHDATE
EOF
}

do_compile()
{
    make clean
    cmake .
    make -j5
    strip nsca
}

do_prepare_deb()
{
    rm -rf debian
    cp -r $DIR_DEBIAN debian
    mkdir -p debian/$PACKAGE_NAME/usr/sbin
    mkdir -p debian/$PACKAGE_NAME/etc
    cp nsca debian/$PACKAGE_NAME/usr/sbin/nsca-fast
    cp nsca.cfg debian/$PACKAGE_NAME/etc/
}

do_make_deb()
{
    dh_shlibdeps
    dh_compress
    dh_link
    fakeroot dh_gencontrol
    dh_installinit
    fakeroot dh_md5sums

    rm -rf Release
    mkdir -p Release
    dh_builddeb --destdir=Release --filename="$RELEASE_PREFIX.deb"
}

do_release_binary()
{
    cp nsca Release/$RELEASE_PREFIX.bin
}

do_cleanup()
{
    make clean
    rm -rf debian
    rm -rf CMakeFiles
    rm -rf Makefile
    rm -rf CMakeCache.txt
}

main()
{
    do_compile
    do_prepare_deb
    do_changelog
    do_make_deb
    do_release_binary
    do_cleanup
}


main
