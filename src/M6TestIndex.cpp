﻿#include <iostream>
#include <ios>
#include <fstream>
#include <map>
#include <algorithm>

#include <boost/filesystem.hpp>
#include <zeep/xml/document.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include "M6Lib.h"
#include "M6File.h"
#include "M6Index.h"
#include "M6Tokenizer.h"
#include "M6Error.h"

#define BOOST_TEST_MAIN
//#define BOOST_TEST_MODULE MyTest
//#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>
//#include <boost/test/minimal.hpp>
//#include <boost/test/included/unit_test.hpp>

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

const char filename[] = "test.index";

//BOOST_AUTO_TEST_CASE(start_up)
//{
//	cout << "Hello, world!" << endl;
//}
//
//BOOST_AUTO_TEST_CASE(zeep_test)
//{
//	zeep::xml::document doc;
//
//	cout << "Hello, world!" << endl;
//}
//
//BOOST_AUTO_TEST_CASE(file_io)
//{
//	const char filename[] = "test-bestand.txt";
//
//	if (fs::exists(filename))
//		fs::remove(filename);
//
//	// read only a non existing file should fail
//	BOOST_CHECK_THROW(M6File(filename, eReadOnly), M6Exception);
//
//	// Create the file
//	M6File file(filename, eReadWrite);
//
//	// check if it exists
//	BOOST_CHECK(fs::exists("test-bestand.txt"));
//	
//	// Reading should fail
//	uint32 i = 0xcececece;
//	BOOST_CHECK_THROW(file.PRead(&i, sizeof(i), 0), M6Exception);
//	BOOST_CHECK_THROW(file.PRead(&i, sizeof(i), 1), M6Exception);
//
//	// Write an int at the start of the file
//	file.PWrite(&i, sizeof(i), 0);
//
//	// File should be one int long
//	BOOST_CHECK_EQUAL(file.Size(), sizeof(i));
//	BOOST_CHECK_EQUAL(fs::file_size(filename), sizeof(i));
//
//	// write another int 1 past the end
//	file.PWrite(&i, sizeof(i), sizeof(i) + 1);
//
//	// File should be two ints plus one long
//	BOOST_CHECK_EQUAL(file.Size(), 2 * sizeof(i) + 1);
//	BOOST_CHECK_EQUAL(fs::file_size(filename), 2 * sizeof(i) + 1);
//
//	file.Truncate(7);
//	BOOST_CHECK_EQUAL(file.Size(), 7);
//	BOOST_CHECK_EQUAL(fs::file_size(filename), 7);
//}
//
//const char* strings[] = {
//	"a", "b", "c", "d", "e", "f", "g", "h",
//	"i", "j", "k",
//	"l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v",
//};
//
//BOOST_AUTO_TEST_CASE(file_ix_1)
//{
//	if (fs::exists(filename))
//		fs::remove(filename);
//
//	int64 nr = 1;
//	
//	M6SimpleIndex indx(filename, eReadWrite);
//	
//	foreach (const char* key, strings)
//		indx.insert(key, nr++);
//
//	indx.validate();
//
//	nr = 1;
//	foreach (const char* key, strings)
//	{
//		int64 v;
//		BOOST_CHECK(indx.find(key, v));
//		BOOST_CHECK_EQUAL(v, nr);
//		++nr;
//	}
//	
//	indx.erase("c");
//	indx.erase("d");
//	indx.erase("m");
//
//	indx.validate();
//	indx.dump();
//}
//
//BOOST_AUTO_TEST_CASE(file_ix_2)
//{
//	M6SimpleIndex indx(filename, eReadOnly);
//
//	int64 nr = 1;
//	foreach (const char* key, strings)
//	{
//		int64 v = nr;
//		BOOST_CHECK_EQUAL(indx.find(key, v), not (*key == 'c' or *key == 'd' or *key == 'm'));
//		BOOST_CHECK_EQUAL(v, nr);
//		++nr;
//	}
//}
//
//BOOST_AUTO_TEST_CASE(file_ix_3)
//{
//	if (fs::exists(filename))
//		fs::remove(filename);
//
//	M6SimpleIndex indx(filename, eReadWrite);
//
//	ifstream text("test/test-doc.txt");
//	BOOST_REQUIRE(text.is_open());
//
//	map<string,int64> testix;
//
//	int64 nr = 1;
//	for (;;)
//	{
//		string word;
//		text >> word;
//
//		if (word.empty() and text.eof())
//			break;
//
//		ba::to_lower(word);
//		
//		if (testix.find(word) != testix.end())
//			continue;
//
//		//if (indx.find(word, v))
//		//	continue;
//		
//		cout << word << endl;
//		indx.insert(word, nr);
//
//		testix[word] = nr;
//
//		++nr;
//	}
//	
//	cout << "Created tree with " << indx.size()
//		<< " keys and a depth of " << indx.depth() << endl;
//
//	foreach (auto t, testix)
//	{
//		int64 v;
//		BOOST_CHECK(indx.find(t.first, v));
//		BOOST_CHECK_EQUAL(v, t.second);
//	}
//	
//	nr = 0;
//	foreach (const M6Tuple& i, indx)
//	{
//		BOOST_CHECK_EQUAL(testix[i.key], i.value);
//		++nr;
//	}
//	
//	BOOST_CHECK_EQUAL(nr, testix.size());
//
////	indx.Vacuum();
//
//	indx.validate();
//
//	foreach (auto t, testix)
//	{
//		int64 v;
//		BOOST_CHECK(indx.find(t.first, v));
//		BOOST_CHECK_EQUAL(v, t.second);
//	}
//	
//	nr = 0;
//	foreach (auto i, indx)
//	{
//		BOOST_CHECK_EQUAL(testix[i.key], i.value);
//		++nr;
//	}
//	
//	BOOST_CHECK_EQUAL(nr, testix.size());
//	BOOST_CHECK_EQUAL(nr, indx.size());
//
//	// remove tests
//
//	vector<string> keys;
//	foreach (auto k, testix)
//		keys.push_back(k.first);
//	random_shuffle(keys.begin(), keys.end());
//
//	ofstream backup("order-of-the-keys.txt");
//
//	copy(keys.begin(), keys.end(), ostream_iterator<string>(backup, "\n"));
//	backup.close();
//
//	for (auto key = keys.begin(); key != keys.end(); ++key)
//	{
//		cout << "erasing " << *key << endl;
//
// 		indx.erase(*key);
//		//indx.validate();
//
//		for (auto test = key + 1; test != keys.end(); ++test)
//		{
//			int64 v;
//			BOOST_CHECK(indx.find(*test, v));
//			BOOST_CHECK_EQUAL(v, testix[*test]);
//		}
//	}
//
//	BOOST_CHECK_EQUAL(indx.size(), 0);
//}
//
//BOOST_AUTO_TEST_CASE(file_ix_4)
//{
//	if (fs::exists(filename))
//		fs::remove(filename);
//
//	ifstream text("test/test-doc-2.txt");
//	BOOST_REQUIRE(text.is_open());
//
//	map<string,int64> testix;
//
//	int64 nr = 1;
//	for (;;)
//	{
//		string word;
//		text >> word;
//
//		if (word.empty() and text.eof())
//			break;
//
//		ba::to_lower(word);
//		
//		testix[word] = nr++;
//	}
//	
//	map<string,int64>::iterator i = testix.begin();
//
//	M6SortedInputIterator data = 
//		[&testix, &i](M6Tuple& outTuple) -> bool
//		{
//			bool result = false;
//			if (i != testix.end())
//			{
//				outTuple.key = i->first;
//				outTuple.value = i->second;
//				++i;
//				result = true;
//			}
//			return result;
//		};
//	
//	M6SimpleIndex indx(filename, data);
//	indx.validate();
//
//	foreach (auto t, testix)
//	{
//		int64 v;
//		BOOST_CHECK(indx.find(t.first, v));
//		BOOST_CHECK_EQUAL(v, t.second);
//	}
//	
//	nr = 0;
//	for (auto i = indx.begin(); i != indx.end(); ++i)
//	{
//		BOOST_CHECK_EQUAL(testix[i->key], i->value);
//		++nr;
//	}
//	
//	BOOST_CHECK_EQUAL(nr, testix.size());
//
//	indx.Vacuum();
//	indx.validate();
//
//	foreach (auto t, testix)
//	{
//		int64 v;
//		BOOST_CHECK(indx.find(t.first, v));
//		BOOST_CHECK_EQUAL(v, t.second);
//	}
//	
//	nr = 0;
//	for (auto i = indx.begin(); i != indx.end(); ++i)
//	{
//		BOOST_CHECK_EQUAL(testix[i->key], i->value);
//		++nr;
//	}
//
//	BOOST_CHECK_EQUAL(nr, testix.size());
//
//	// remove tests
//
//	vector<string> keys;
//	foreach (auto k, testix)
//		keys.push_back(k.first);
//	random_shuffle(keys.begin(), keys.end());
//
//	for (auto key = keys.begin(); key != keys.end(); ++key)
//	{
//		cout << "erasing key " << *key << endl;
//
//		indx.erase(*key);
//
//		for (auto test = key + 1; test != keys.end(); ++test)
//		{
//			int64 v;
//			BOOST_CHECK(indx.find(*test, v));
//			BOOST_CHECK_EQUAL(v, testix[*test]);
//		}
//	}
//
//	BOOST_CHECK_EQUAL(indx.size(), 0);
//}	

//BOOST_AUTO_TEST_CASE(test_tokenizer)
//{
//	if (fs::exists(filename))
//		fs::remove(filename);
//
//	ifstream text("test/test-doc.txt");
//	BOOST_REQUIRE(text.is_open());
//
//	for (;;)
//	{
//		string line;
//		getline(text, line);
//		
//		if (line.empty())
//		{
//			if (text.eof())
//				break;
//			continue;
//		}
//		
//		M6Tokenizer tokenizer(line.c_str(), line.length());
//		for (;;)
//		{
//			M6Token token = tokenizer.GetToken();
//			if (token == eM6TokenEOF)
//				break;
//			
//			switch (token)
//			{
//				case eM6TokenWord:
//					cout << "word:   '" << tokenizer.GetTokenString() << '\'' << endl;
//					break;
//				
//				case eM6TokenNumber:
//					cout << "number: '" << tokenizer.GetTokenString() << '\'' << endl;
//					break;
//				
//				case eM6TokenPunctuation:
//					cout << "punct:  '" << tokenizer.GetTokenString() << '\'' << endl;
//					break;
//				
//				case eM6TokenOther:
//					cout << "other:  '" << tokenizer.GetTokenString() << '\'' << endl;
//					break;
//			}
//		}
//	}
//}

BOOST_AUTO_TEST_CASE(file_ix_5a)
{
	if (fs::exists(filename))
		fs::remove(filename);

	ifstream text("test/test-doc-2.txt");
	BOOST_REQUIRE(text.is_open());

	map<string,int64> testix;

	int64 nr = 1;
	for (;;)
	{
		string word;
		text >> word;

		if (word.empty() and text.eof())
			break;

		ba::to_lower(word);
		
		testix[word] = nr++;
	}
	
	map<string,int64>::iterator i = testix.begin();

	M6SortedInputIterator data = 
		[&testix, &i](M6Tuple& outTuple) -> bool
		{
			bool result = false;
			if (i != testix.end())
			{
				outTuple.key = i->first;
				outTuple.value = i->second;
				++i;
				result = true;
			}
			return result;
		};
	
	M6SimpleIndex indx(filename, data);
	indx.validate();
}

BOOST_AUTO_TEST_CASE(file_ix_5b)
{
	ifstream text("test/test-doc-2.txt");
	BOOST_REQUIRE(text.is_open());

	map<string,int64> testix;

	int64 nr = 1;
	for (;;)
	{
		string word;
		text >> word;

		if (word.empty() and text.eof())
			break;

		ba::to_lower(word);
		
		testix[word] = nr++;
	}
	
	M6SimpleIndex indx(filename, eReadWrite);
	indx.validate();

	foreach (auto t, testix)
	{
		int64 v;
		BOOST_CHECK(indx.find(t.first, v));
		BOOST_CHECK_EQUAL(v, t.second);
	}
	
	nr = 0;
	foreach (auto i, indx)
	{
		BOOST_CHECK_EQUAL(testix[i.key], i.value);
		++nr;
	}
	
	BOOST_CHECK_EQUAL(nr, testix.size());
}	

BOOST_AUTO_TEST_CASE(file_ix_5c)
{
	ifstream text("test/test-doc-2.txt");
	BOOST_REQUIRE(text.is_open());

	map<string,int64> testix;

	int64 nr = 1;
	for (;;)
	{
		string word;
		text >> word;

		if (word.empty() and text.eof())
			break;

		ba::to_lower(word);
		
		testix[word] = nr++;
	}
	
	M6SimpleIndex indx(filename, eReadWrite);
	indx.validate();

	foreach (auto t, testix)
	{
		indx.erase(t.first);
		//indx.validate();
		int64 v;
		BOOST_CHECK_EQUAL(indx.find(t.first, v), false);
	}
	
	BOOST_CHECK_EQUAL(indx.size(), 0);
}	

//BOOST_AUTO_TEST_CASE(file_ix_1a)
//{
//	if (fs::exists(filename))
//		fs::remove(filename);
//
//	int64 nr = 0;
//
//	boost::format nf("%04.4d");
//	
//	M6SortedInputIterator data = 
//		[&nr, &nf](M6Tuple& outTuple) -> bool
//		{
//			bool result = false;
//			if (++nr <= 100)
//			{
//				outTuple.key = (nf % nr).str();
//				outTuple.value = nr;
//				result = true;
//			}
//			return result;
//		};	
//
//	M6SimpleIndex indx(filename, data);
//	indx.validate();
//	indx.dump();
//	
//	for (;;)
//	{
//		cout << "> "; cout.flush();
//		int i;
//		cin >> i;
//		if (cin.eof() or i == 0)
//			break;
//			
//		if (i > 0)
//			indx.insert((nf % i).str(), i);
//		else
//			indx.erase((nf % -i).str());
//
//		indx.dump();
//		indx.validate();
//
//		foreach (auto i, indx)
//		{
//			int64 v;
//			BOOST_CHECK(indx.find(i.key, v));
//			BOOST_CHECK_EQUAL(v, i.value);
//		}
//	}
//}
