cmake -DWITH_DEBUG=1 -DWITH_SSL=system -DWITH_NDBCLUSTER=1 -DWITH_NDB_TEST=1 -DWITH_NDBAPI_EXAMPLES=1 -DWITH_ROUTER=0 -DWITH_UNIT_TESTS=1 -DWITH_ERROR_INSERT=1 _DWITH_BOOST=$BOOST_ROOT -DWITH_NDB_JAVA=1 -DWITH_LIBEVENT=bundled ../
make -j16
