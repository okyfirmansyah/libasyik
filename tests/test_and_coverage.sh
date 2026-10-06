cd /usr/local/libasyik
# Report library code only (include/ and src/): test sources, Catch2 and
# other external code would only inflate the number.
(timeout 300 ./build/tests/libasyik_test) && \
  (lcov --capture --directory . --no-external --output-file /tmp/all.info) && \
  (lcov --extract /tmp/all.info '/usr/local/libasyik/include/*' '/usr/local/libasyik/src/*' \
        --output-file /mnt/coverage/libasyik_test.info)
