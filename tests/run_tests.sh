#echo "A very simple test"
#./tests/simple_test.sh
# Run the same test, but this time with valgrind
echo "A very simple test, with Valgrind"
VALGRIND=1 ./tests/main_tests.sh
