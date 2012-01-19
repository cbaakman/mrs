#include <iostream>
#include <ios>
#include <fstream>
#include <boost/filesystem.hpp>
#include <zeep/xml/document.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6Lib.h"
#include "M6File.h"
#include "M6Index.h"
#include "M6Error.h"

#define BOOST_TEST_MAIN
#include <boost/test/unit_test.hpp>

using namespace std;
namespace fs = boost::filesystem;

BOOST_AUTO_TEST_CASE(start_up)
{
	cout << "Hello, world!" << endl;
}

BOOST_AUTO_TEST_CASE(zeep_test)
{
	zeep::xml::document doc;

	cout << "Hello, world!" << endl;
}

BOOST_AUTO_TEST_CASE(file_io)
{
	const char filename[] = "test-bestand.txt";

	if (fs::exists(filename))
		fs::remove(filename);

	// read only a non existing file should fail
	BOOST_CHECK_THROW(M6File(filename, eReadOnly), M6Exception);

	// Create the file
	M6File file(filename, eReadWrite);

	// check if it exists
	BOOST_CHECK(fs::exists("test-bestand.txt"));
	
	// Reading should fail
	uint32 i = 0xcececece;
	BOOST_CHECK_THROW(file.PRead(&i, sizeof(i), 0), M6Exception);
	BOOST_CHECK_THROW(file.PRead(&i, sizeof(i), 1), M6Exception);

	// Write an int at the start of the file
	file.PWrite(&i, sizeof(i), 0);

	// File should be one int long
	BOOST_CHECK_EQUAL(file.Size(), sizeof(i));
	BOOST_CHECK_EQUAL(fs::file_size(filename), sizeof(i));

	// write another int 1 past the end
	file.PWrite(&i, sizeof(i), sizeof(i) + 1);

	// File should be two ints plus one long
	BOOST_CHECK_EQUAL(file.Size(), 2 * sizeof(i) + 1);
	BOOST_CHECK_EQUAL(fs::file_size(filename), 2 * sizeof(i) + 1);

	file.Truncate(7);
	BOOST_CHECK_EQUAL(file.Size(), 7);
	BOOST_CHECK_EQUAL(fs::file_size(filename), 7);

	// clean up
	fs::remove(filename);
}

BOOST_AUTO_TEST_CASE(file_ix_1)
{
	const char filename[] = "test.index";

	if (fs::exists(filename))
		fs::remove(filename);

	const char* strings[] = {
		"aap", "noot", "mies", "boom", "roos", "vis", "vuur", "water"
	};
	int64 nr = 1;
	
	M6IndexBase indx(filename, true);
	
	foreach (const char* key, strings)
		indx.Insert(key, nr++);
}
