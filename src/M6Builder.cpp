#include "M6Lib.h"

#include <iostream>
#include <memory>
#include <list>
#include <cctype>
#include <functional>

#include "boost/date_time/posix_time/posix_time.hpp"
#include "boost/date_time/local_time/local_time.hpp"
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/iostreams/device/back_inserter.hpp>
#include <boost/iostreams/filtering_stream.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/format.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
//#include <boost/timer/timer.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/pool/pool.hpp>
#include <boost/thread/tss.hpp>

#include <zeep/xml/xpath.hpp>
#include <zeep/xml/writer.hpp>

#include "M6DocStore.h"
#include "M6Error.h"
#include "M6Databank.h"
#include "M6Document.h"
#include "M6Builder.h"
#include "M6Config.h"
#include "M6Progress.h"
#include "M6DataSource.h"
#include "M6Queue.h"
#include "M6Config.h"
#include "M6Exec.h"
#include "M6Parser.h"
#include "M6Utilities.h"

using namespace std;
using namespace std::tr1;
namespace zx = zeep::xml;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;
namespace io = boost::iostreams;

// --------------------------------------------------------------------

class M6Processor
{
  public:
	typedef M6Queue<fs::path>					M6FileQueue;
	typedef M6Queue<tr1::tuple<string,string>>	M6DocQueue;

	static const tr1::tuple<string,string> kSentinel;

					M6Processor(M6Databank& inDatabank, M6Lexicon& inLexicon,
						const zx::element* inTemplate);
	virtual			~M6Processor();
	
	void			Process(vector<fs::path>& inFiles, M6Progress& inProgress,
						uint32 inNrOfThreads);
	
	M6InputDocument*
					IndexDocument(const string& inText, const string& inFileName);
	
  private:
	void			ProcessFile(const string& inFileName, istream& inFileStream);

	void			ParseFile(const string& inFileName, istream& inFileStream);
	void			ParseXML(const string& inFileName, istream& inFileStream);
	void			ParseNode(M6InputDocument& inDoc, zx::node* inNode,
						const string& inIndex, M6DataType inDataType, bool isUnique);

	void			ProcessFile(M6Progress& inProgress);
	void			ProcessDocument();
	void			ProcessDocument(const string& inDoc);

	void			PutDocument(const string& inDoc)
					{
						if (not (mException == exception_ptr()))
							rethrow_exception(mException);
						
						if (mUseDocQueue)
							mDocQueue.Put(tr1::make_tuple(inDoc, *mFileName));
						else
							ProcessDocument(inDoc);
					}

	void			Error(exception_ptr e);

	struct XMLIndex
	{
		string		name;
		zx::xpath	xpath;
		bool		unique;
		M6DataType	type;
		bool		attr;
	};
	
	M6Databank&				mDatabank;
	M6Lexicon&				mLexicon;
	const zx::element*		mConfig;
	M6Parser*				mParser;
	vector<XMLIndex>		mXMLIndexInfo;
	string					mChunkXPath;
	M6FileQueue				mFileQueue;
	M6DocQueue				mDocQueue;
	bool					mUseDocQueue;
	bool					mWriteFasta;
	fs::ofstream			mFastaFile;
	string					mDbHeader;
	boost::thread_specific_ptr<string>
							mFileName;
	boost::thread_group		mFileThreads, mDocThreads;
	exception_ptr			mException;
};

const tr1::tuple<string,string> M6Processor::kSentinel;

// --------------------------------------------------------------------

M6Processor::M6Processor(M6Databank& inDatabank, M6Lexicon& inLexicon,
		const zx::element* inTemplate)
	: mDatabank(inDatabank), mLexicon(inLexicon), mConfig(inTemplate), mParser(nullptr)
{
	string parser = mConfig->get_attribute("parser");
	if (parser.empty())
		THROW(("Missing parser attribute"));
	
	// see if this is an XML parser
	const zx::element* p = M6Config::GetParser(parser);
	if (p == nullptr)
		mParser = new M6Parser(parser);
	else
	{
		mChunkXPath = p->get_attribute("chunk");
		if (mChunkXPath.empty())
			THROW(("Missing chunk XPath attribute in XML parser"));
		
		foreach (zx::element* ix, p->find("index"))
		{
			string tt = ix->get_attribute("type");
			M6DataType type = eM6TextData;
			if (tt == "string")
				type = eM6StringData;
			else if (tt == "number")
				type = eM6NumberData;
			
			XMLIndex info = {
				ix->get_attribute("name"),
				ix->get_attribute("xpath"),
				ix->get_attribute("unique") == "true",
				type,
				ix->get_attribute("attr") == "true"
			};
			
			mXMLIndexInfo.push_back(info);
		}
	}
	
	mWriteFasta = mConfig->get_attribute("fasta") == "true";
}

M6Processor::~M6Processor()
{
	delete mParser;
}

void M6Processor::Error(exception_ptr e)
{
	mException = e;
	mFileThreads.interrupt_all();
	mDocThreads.interrupt_all();
}

struct M6LineMatcher
{
  public:
			M6LineMatcher(const string& inMatch)
				: mStr(inMatch)
			{
				if (ba::starts_with(mStr, "(?^:"))
					mRE.assign(mStr.substr(4, mStr.length() - 5));
			}
	
	bool	Match(const string& inStr) const
			{
				bool result = false;
				
				if (mRE.empty())
					result = mStr.empty() == false and mStr == inStr;
				else
					result = boost::regex_match(inStr, mRE);
				
				return result;
			}
	
			operator bool() { return mRE.empty() == false or mStr.empty() == false; }
	
	string			mStr;
	boost::regex	mRE;
};

void M6Processor::ProcessFile(const string& inFileName, istream& inFileStream)
{
	mFileName.reset(new string(inFileName));
	
	io::filtering_stream<io::input> in;

	if (mConfig->find_first("filter"))
		in.push(M6Process(mConfig->find_first("filter")->content(), inFileStream));
	else
		in.push(inFileStream);

	try
	{
		if (mParser != nullptr)
			ParseFile(inFileName, in);
		else
			ParseXML(inFileName, in);
	}
	catch (exception& e)
	{
		cerr << endl
			 << "Error parsing file " << inFileName << endl
			 << e.what() << endl;
		Error(current_exception());
	}
}

void M6Processor::ParseNode(M6InputDocument& inDoc, zx::node* inNode,
	const string& inIndex, M6DataType inDataType, bool isUnique)
{
	zx::element* el = dynamic_cast<zx::element*>(inNode);
	if (el == nullptr)
	{
		string text = inNode->str();
		inDoc.Index(inIndex, inDataType, isUnique, text.c_str(), text.length());
	}
	else
	{
		foreach (zx::node* node, el->nodes())
			ParseNode(inDoc, node, inIndex, inDataType, isUnique);
	}
}

void M6Processor::ParseXML(const string& inFileName, istream& inFileStream)
{
	// simple case first, just parse the entire document

	zx::process_document_elements(inFileStream, mChunkXPath, [&] (zx::node* root, zx::element* xml) -> bool
	{
		stringstream text;
		zx::writer w(text);
		xml->write(w);
		
		unique_ptr<M6InputDocument> doc(new M6InputDocument(mDatabank, text.str()));
		
		foreach (XMLIndex& ix, mXMLIndexInfo)
		{
			foreach (zx::node* n, ix.xpath.evaluate<zx::node>(*xml))
			{
				if (ix.attr)
				{
					string text = n->str();
				
					doc->Index(ix.name, ix.type, ix.unique, text.c_str(), text.length());
					doc->SetAttribute(ix.name, text.c_str(), text.length());
				}
				else
					ParseNode(*doc, n, ix.name, ix.type, ix.unique);
			}
		}
		
		doc->Tokenize(mLexicon, 0);
		doc->Compress();
		
		mDatabank.Store(doc.release());
		
		return true;
	});
}

void M6Processor::ParseFile(const string& inFileName, istream& inFileStream)
{
	M6LineMatcher header(mParser->GetValue("header")),
				  lastheaderline(mParser->GetValue("lastheaderline")),
				  trailer(mParser->GetValue("trailer")),
				  firstline(mParser->GetValue("firstdocline")),
				  lastline(mParser->GetValue("lastdocline"));
				  
	enum State { eHeader, eStart, eDoc, eTail } state = eHeader;
	
	if (not header and not lastheaderline)
		state = eStart;

	string document, line;
	
	while (state != eTail)
	{
		line.clear();
		getline(inFileStream, line);

		if (ba::ends_with(line, "\r"))
			line.erase(line.end() - 1);

		if (line.empty() and inFileStream.eof())
		{
			if (not document.empty())
				PutDocument(document);
			break;
		}
		
		switch (state)
		{
			case eHeader:
				mDbHeader += line + '\n';
				if (lastheaderline)
				{
					if (lastheaderline.Match(line))
						state = eStart;
				}
				else if (header and header.Match(line))
					break;
				// else fall through
			
			case eStart:
				if (not firstline or firstline.Match(line))
				{
					document = line + '\n';
					state = eDoc;
				}
				else if (trailer and trailer.Match(line))
					state = eTail;
				break;
			
			case eDoc:
				if (not lastline and firstline and firstline.Match(line))
				{
					PutDocument(document);
					document = line + '\n';
				}
				else if (trailer and trailer.Match(line))
				{
					if (not document.empty())
						PutDocument(document);
					state = eTail;
				}
				else
				{
					document += line + '\n';
					if (lastline and lastline.Match(line))
					{
						PutDocument(document);
						document.clear();
						state = eStart;
					}
				}
				break;
			
			case eTail:
				break;
		}
	}
}

void M6Processor::ProcessFile(M6Progress& inProgress)
{
	try
	{
		for (;;)
		{
			fs::path path = mFileQueue.Get();
			if (path.empty())
				break;
			
			try
			{
				M6DataSource data(path, inProgress);
				for (M6DataSource::iterator i = data.begin(); i != data.end(); ++i)
					ProcessFile(i->mFilename, i->mStream);
			}
			catch (exception& e)
			{
				cerr << endl
					 << "Error processsing " << path << endl
					 << e.what() << endl;
			}
		}
		
		mFileQueue.Put(fs::path());
	}
	catch (exception&)
	{
		Error(current_exception());
	}
}

void M6Processor::ProcessDocument(const string& inDoc)
{
	M6InputDocument* doc = new M6InputDocument(mDatabank, inDoc);
	
	mParser->ParseDocument(doc, *mFileName, mDbHeader);
	if (mWriteFasta)
	{
		string fasta;
		mParser->ToFasta(inDoc, mConfig->get_attribute("id"),
			doc->GetAttribute("id"), doc->GetAttribute("title"), fasta);
		if (not fasta.empty())
			doc->SetFasta(fasta);
	}
	
	doc->Tokenize(mLexicon, 0);
	doc->Compress();
	
	mDatabank.Store(doc);
}

M6InputDocument* M6Processor::IndexDocument(const string& inDoc, const string& inFileName)
{
	M6InputDocument* doc = new M6InputDocument(mDatabank, inDoc);
	
	mParser->ParseDocument(doc, inFileName, mDbHeader);
	
	doc->Tokenize(mLexicon, 0);
	return doc;
}

void M6Processor::ProcessDocument()
{
	try
	{
		unique_ptr<M6Lexicon> tsLexicon(new M6Lexicon);
		vector<M6InputDocument*> docs;
		
		for (;;)
		{
			string text, filename;
			tr1::tie(text, filename) = mDocQueue.Get();
			
			if (text.empty() or docs.size() == 100)
			{
				// remap tokens
				vector<uint32> remapped(tsLexicon->Count() + 1, 0);
	
				{
					M6Lexicon::M6SharedLock sharedLock(mLexicon);
					
					for (uint32 t = 1; t < tsLexicon->Count(); ++t)
					{
						const char* w;
						size_t l;
						tsLexicon->GetString(t, w, l);
						remapped[t] = mLexicon.Lookup(w, l);
					}
				}
				
				{
					M6Lexicon::M6UniqueLock uniqueLock(mLexicon);
				
					for (uint32 t = 1; t < tsLexicon->Count(); ++t)
					{
						if (remapped[t] != 0)
							continue;
						
						const char* w;
						size_t l;
						tsLexicon->GetString(t, w, l);
						remapped[t] = mLexicon.Store(w, l);
					}
				}
				
				foreach (M6InputDocument* doc, docs)
				{
					doc->RemapTokens(&remapped[0]);
					mDatabank.Store(doc);
				}
				
				docs.clear();
				tsLexicon.reset(new M6Lexicon);
			}
			
			if (text.empty())
				break;
	
			M6InputDocument* doc = new M6InputDocument(mDatabank, text);
			
			mParser->ParseDocument(doc, filename, mDbHeader);
			if (mWriteFasta)
			{
				string fasta;
				mParser->ToFasta(text, mConfig->get_attribute("id"),
					doc->GetAttribute("id"), doc->GetAttribute("title"), fasta);
				if (not fasta.empty())
					doc->SetFasta(fasta);
			}
			
			doc->Tokenize(*tsLexicon, 0);
			doc->Compress();
			docs.push_back(doc);
		}
		
		assert(docs.empty());
		
		mDocQueue.Put(kSentinel);
	}
	catch (exception&)
	{
		Error(current_exception());
	}
}

void M6Processor::Process(vector<fs::path>& inFiles, M6Progress& inProgress,
	uint32 inNrOfThreads)
{
	if (inFiles.size() >= inNrOfThreads)
		mUseDocQueue = false;
	else
	{
		mUseDocQueue = true;
		for (uint32 i = 0; i < inNrOfThreads; ++i)
			mDocThreads.create_thread([this]() { this->ProcessDocument(); });
	}

	if (inFiles.size() == 1)
	{
		M6DataSource data(inFiles.front(), inProgress);
		for (M6DataSource::iterator i = data.begin(); i != data.end(); ++i)
			ProcessFile(i->mFilename, i->mStream);
	}
	else
	{
		if (inNrOfThreads > inFiles.size())
			inNrOfThreads = inFiles.size();

		for (uint32 i = 0; i < inNrOfThreads; ++i)
			mFileThreads.create_thread([&inProgress, this]() { this->ProcessFile(inProgress); });

		foreach (fs::path& file, inFiles)
		{
			if (not (mException == std::exception_ptr()))
				rethrow_exception(mException);

			if (not fs::exists(file))
			{
				cerr << "file missing: " << file << endl;
				continue;
			}
			
			mFileQueue.Put(file);
		}

		mFileQueue.Put(fs::path());
		mFileThreads.join_all();
	}
	
	if (mUseDocQueue)
	{
		mDocQueue.Put(kSentinel);
		mDocThreads.join_all();
	}

	if (not (mException == std::exception_ptr()))
		rethrow_exception(mException);
}

// --------------------------------------------------------------------

M6Builder::M6Builder(const string& inDatabank)
	: mConfig(M6Config::GetEnabledDatabank(inDatabank))
	, mDatabank(nullptr)
{
	if (mConfig == nullptr)
		THROW(("Databank %s not known or not enabled", inDatabank.c_str()));
}

M6Builder::~M6Builder()
{
	delete mDatabank;
}

int64 M6Builder::Glob(boost::filesystem::path inRawDir,
	zx::element* inSource, vector<fs::path>& outFiles)
{
	int64 result = 0;
	
	if (inSource == nullptr)
		THROW(("No source specified for databank"));

	string source = inSource->content();
	ba::trim(source);

	vector<string> paths;
	ba::split(paths, source, ba::is_any_of(";"));
	
	foreach (string& source, paths)
	{
		fs::path dir = fs::path(source).parent_path();
		if (not dir.has_root_path())
		{
			dir = (inRawDir / dir).make_preferred();
			source = (inRawDir / fs::path(source)).make_preferred().string();
		}
	
		while (not dir.empty() and (ba::contains(dir.filename().string(), "?") or ba::contains(dir.filename().string(), "*")))
			dir = dir.parent_path();
	
		stack<fs::path> ds;
		ds.push(dir);
		while (not ds.empty())
		{
			fs::path dir = ds.top();
			ds.pop();
			
			if (not fs::is_directory(dir))
				THROW(("'%s' is not a directory", dir.string().c_str()));
			
			fs::directory_iterator end;
			for (fs::directory_iterator i(dir); i != end; ++i)
			{
				if (fs::is_directory(*i))
					ds.push(*i);
				else if (M6FilePathNameMatches(*i, source))
				{
					result += fs::file_size(*i);
					outFiles.push_back(*i);
				}
			}
		}
	}
	
 	return result;
}

void M6Builder::Build(uint32 inNrOfThreads)
{
	string dbID = mConfig->get_attribute("id");
	
//	boost::timer::auto_cpu_timer t;

	fs::path path = M6Config::GetDbDirectory(dbID), dstPath = path;

	if (fs::exists(path))
	{
		boost::uuids::random_generator gen;
		boost::uuids::uuid u = gen();
		
		path = path.string() + "-" + boost::lexical_cast<string>(u);
	}

	zx::element* source = mConfig->find_first("source");
	if (source == nullptr)
		THROW(("Missing source specification for databank '%s'", dbID.c_str()));
	
	string version = M6Parser::GetVersion(mConfig->get_attribute("parser"),
		source->content());
	
	// TODO fetch version string?
	
	mDatabank = M6Databank::CreateNew(dbID, path.string(), version);
	mDatabank->StartBatchImport(mLexicon);
	
	vector<fs::path> files;
	int64 rawBytes = Glob(M6Config::GetDirectory("raw"), source, files);
	
	{
		M6Progress progress(dbID, rawBytes + 1, "parsing");
	
		M6Processor processor(*mDatabank, mLexicon, mConfig);
		processor.Process(files, progress, inNrOfThreads);

		mDatabank->EndBatchImport();
	}
	
	mDatabank->FinishBatchImport();
	
	delete mDatabank;
	mDatabank = nullptr;
	
	// if we created a temporary db
	if (path != dstPath)
	{
		fs::remove_all(dstPath);
		fs::rename(path, dstPath);
	}
	
	cout << "done" << endl;
}

void M6Builder::IndexDocument(const string& inText, const string& inFileName,
	vector<string>& outTerms)
{
	M6Processor processor(*mDatabank, mLexicon, mConfig);
	unique_ptr<M6InputDocument> doc(processor.IndexDocument(inText, inFileName));
	
	foreach (auto& list, doc->GetIndexTokens())
	{
		foreach (auto& token, list.mTokens)
		{
			if (token != 0)
				outTerms.push_back(mLexicon.GetString(token));
		}
	}
}

bool M6Builder::NeedsUpdate()
{
	bool result = true;
	
	fs::path path = M6Config::GetDbDirectory(mConfig->get_attribute("id"));
	if (fs::exists(path))
	{
		result = false;
		
		time_t dbTime = fs::last_write_time(path);
		
		vector<fs::path> files;
		Glob(M6Config::GetDirectory("raw"), mConfig->find_first("source"), files);
		
		foreach (fs::path& file, files)
		{
			if (fs::last_write_time(file) > dbTime)
			{
				result = true;
				break;
			}
		}
	}
	
	return result;
}

// --------------------------------------------------------------------

M6Scheduler::M6Scheduler()
	: mThread(boost::bind(&M6Scheduler::Run, this))
{
}

M6Scheduler::~M6Scheduler()
{
	if (mThread.joinable())
	{
		mThread.interrupt();
		mThread.join();
	}	
}

M6Scheduler& M6Scheduler::Instance()
{
	static M6Scheduler sInstance;
	return sInstance;
}
	
void M6Scheduler::Schedule(const string& inDatabank, const char* inAction)
{
	boost::mutex::scoped_lock lock(mLock);
	
	if (find_if(mScheduled.begin(), mScheduled.end(), [inDatabank](tr1::tuple<string,string>& s) -> bool
			{ return tr1::get<0>(s) == inDatabank; }) == mScheduled.end())
	{
		mScheduled.push_back(tr1::make_tuple(inDatabank, inAction));
	}
}

void M6Scheduler::GetScheduledDatabanks(vector<string>& outDatabanks)
{
	outDatabanks.clear();
	
	boost::mutex::scoped_lock lock(mLock);
	
	for_each(mScheduled.begin(), mScheduled.end(), [&outDatabanks](tr1::tuple<string,string>& s) {
		outDatabanks.push_back(tr1::get<0>(s));
	});
}

void M6Scheduler::Run()
{
    using namespace boost::local_time;
    using namespace boost::posix_time;

	fs::ofstream log(fs::path(M6Config::GetDirectory("log")) / "build.log");
	if (not log.is_open())
	{
		cerr << "Failed to create log file" << endl;
		exit(1);
	}

	bool enabled;
	ptime updateTime;
	string updateWeekday;
	M6Config::GetSchedule(enabled, updateTime, updateWeekday);

	if (not enabled)
		return;

	ptime now = second_clock::local_time();
	ptime start = updateTime;
    if (start < now)
    	start += hours(24);
	time_iterator update(start, hours(24));		// daily 
	bool writeNextUpdateTime = true, reload = false;

	for (;;)
	{
		if (writeNextUpdateTime)
		{
			log << "Next update at " << *update << endl;
			writeNextUpdateTime = false;
		}
		
		if (reload)
		{
			M6SignalCatcher::Signal(SIGHUP);
			reload = false;		
		}
		
		boost::this_thread::sleep(boost::posix_time::seconds(5));
		string databank, action;

		try
		{
			now = second_clock::local_time();
			if (now > *update)
			{
				do ++update; while (*update < now);

				writeNextUpdateTime = true;
				
				bool weekly = ba::iequals(
					now.date().day_of_week().as_long_string(), updateWeekday);
				bool monthly = weekly and now.date().day() < 7;
				
				foreach (zx::element* db, M6Config::GetDatabanks())
				{
					if (db->get_attribute("enabled") != "true")
						continue;
					
					string id = db->get_attribute("id");
					string update = db->get_attribute("update");
					
					if (update == "daily" or
						(update == "weekly" and weekly) or
						(update == "monthly" and monthly))
					{
						Schedule(id);
					}
				}
			}			

			for (;;)
			{
				databank.clear();
				action.clear();

				mLock.lock();
				if (not mScheduled.empty())
				{
					tr1::tie(databank, action) = mScheduled.front();
					mScheduled.pop_front();
				}
				mLock.unlock();
				
				if (databank.empty())
					break;
				
				fs::path m6(GetExecutablePath());
				
				string exe = m6.string();
				vector<const char*> args;
				args.push_back(exe.c_str());
				args.push_back(action.c_str());
				args.push_back(databank.c_str());
				args.push_back(nullptr);
				
				log << "About to " << action << ' ' << databank << endl;
				
				stringstream in;
				int r = ForkExec(args, 0, in, log, log);
				
				if (r == 0)
					reload = true;	// signal to reload databanks
				else
					log << action << " of " << databank << " returned: " << r << endl;
				
				log << endl;
			}
		}
		catch (boost::thread_interrupted& e)
		{
			M6Status::Instance().SetError(databank, "interrupted");
			log << "Stopping scheduler on interrupt" << endl;
			break;
		}
		catch (exception& e)
		{
			M6Status::Instance().SetError(databank, e.what());
			log << "Exception in scheduler:" << endl
				 << e.what() << endl;
		}	
	}
}
