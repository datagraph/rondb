cmake -DWITH_DEBUG=0 -DWITH_SSL=$OPENSSL_ROOT -DWITH_NDBCLUSTER=1 -DWITH_NDB_TEST=1 -DWITH_NDBAPI_EXAMPLES=1 -DWITH_ROUTER=0 -DWITH_MEB=0 -DWITH_UNIT_TESTS=1 -DWITH_ERROR_INSERT=0 _DWITH_BOOST=$BOOST_ROOT -DCMAKE_INSTALL_PREFIX=${PREFIX_DIR} -DCPACK_MONOLITHIC_INSTALL=true -DBUILD_CONFIG=mysql_release ../
make -j8
make install
