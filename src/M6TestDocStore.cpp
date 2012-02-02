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
#include <boost/timer/timer.hpp>

#include "M6Lib.h"
#include "M6File.h"
#include "M6Index.h"
#include "M6Tokenizer.h"
#include "M6Error.h"
#include "M6Lexicon.h"
#include "M6DocStore.h"
//#include "M6Document.h"

#include <boost/test/unit_test.hpp>

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;
namespace io = boost::iostreams;

BOOST_AUTO_TEST_CASE(test_store_0)
{
	cout << "testing document store (null: testing read time)" << endl;

	boost::timer::auto_cpu_timer t;

	ifstream text("test/pdbfind2-head.txt");
	BOOST_REQUIRE(text.is_open());

	stringstream doc;
	uint32 n = 0;

	for (;;)
	{
		string line;
		getline(text, line);

		if (line.empty())
		{
			if (text.eof())
				break;
			continue;
		}
		
		doc << line << endl;
		
		if (line == "//")
		{
			string document(doc.str());
			++n;
			
			doc.str("");
			doc.clear();
		}
	}
}

BOOST_AUTO_TEST_CASE(test_store_1)
{
	cout << "testing document store (store)" << endl;

	boost::timer::auto_cpu_timer t;

	ifstream text("test/pdbfind2-head.txt");
	BOOST_REQUIRE(text.is_open());

	if (fs::exists("test/pdbfind2.docs"))
		fs::remove("test/pdbfind2.docs");
	
	M6DocStore store("test/pdbfind2.docs", eReadWrite);
	stringstream doc;
	uint32 n = 0;

	for (;;)
	{
		string line;
		getline(text, line);

		if (line.empty())
		{
			if (text.eof())
				break;
			continue;
		}
		
		doc << line << endl;
		
		if (line == "//")
		{
//			M6Document document;
//			document.SetText(doc.str());
//			store.StoreDocument(&document);

			string document(doc.str());
			store.StoreDocument(document.c_str(), document.length());
			++n;
			
			doc.str("");
			doc.clear();
		}
	}
	
	BOOST_CHECK_EQUAL(store.size(), n);
}

BOOST_AUTO_TEST_CASE(test_store_2)
{
	cout << "testing document store (retrieve-1)" << endl;

	boost::timer::auto_cpu_timer t;

	ifstream text("test/pdbfind2-head.txt");
	BOOST_REQUIRE(text.is_open());

	M6DocStore store("test/pdbfind2.docs", eReadOnly);
	stringstream doc;
	uint32 n = 1;

	for (;;)
	{
		string line;
		getline(text, line);

		if (line.empty())
		{
			if (text.eof())
				break;
			continue;
		}
		
		doc << line << endl;
		
		if (line == "//")
		{
			uint32 docPage, docSize;
			BOOST_CHECK(store.FetchDocument(n, docPage, docSize));
			
			io::filtering_stream<io::input> is;
			store.OpenDataStream(n, docPage, docSize, is);
			
			string docA;
			for (;;)
			{
				string line;
				getline(is, line);
				if (line.empty() and is.eof())
					break;
				docA += line + "\n";
			}

			string docB = doc.str();

			BOOST_CHECK_EQUAL(docA.length(), docB.length());
			BOOST_CHECK_EQUAL(docA, docB);

			++n;
			
			doc.str("");
			doc.clear();
		}
	}
	
	BOOST_CHECK_EQUAL(store.size(), n - 1);
}

BOOST_AUTO_TEST_CASE(test_store_3)
{
	cout << "testing document store (retrieve-2 one line only)" << endl;

	boost::timer::auto_cpu_timer t;

	ifstream text("test/pdbfind2-head.txt");
	BOOST_REQUIRE(text.is_open());

	M6DocStore store("test/pdbfind2.docs", eReadOnly);
	stringstream doc;
	uint32 n = 1;

	for (;;)
	{
		string line;
		getline(text, line);

		if (line.empty())
		{
			if (text.eof())
				break;
			continue;
		}
		
		doc << line << endl;
		
		if (line == "//")
		{
			uint32 docPage, docSize;
			BOOST_CHECK(store.FetchDocument(n, docPage, docSize));
			
			io::filtering_stream<io::input> is;
			store.OpenDataStream(n, docPage, docSize, is);
			
			string line;
			getline(is, line);

			++n;
			
			doc.str("");
			doc.clear();
		}
	}
	
	BOOST_CHECK_EQUAL(store.size(), n - 1);
}
