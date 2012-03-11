#include "M6Lib.h"

#include <iostream>

#include <boost/bind.hpp>
#include <boost/tr1/cmath.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string.hpp>

#include "M6Databank.h"
#include "M6Server.h"
#include "M6Error.h"
#include "M6Iterator.h"
#include "M6Document.h"
#include "M6Config.h"
#include "M6Query.h"
#include "M6Tokenizer.h"

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

const string kM6ServerNS = "http://mrs.cmbi.ru.nl/mrs-web/ml";

// --------------------------------------------------------------------

struct M6Redirect
{
	string	db;
	uint32	nr;
};

// --------------------------------------------------------------------

M6Server::M6Server(zx::element* inConfig)
	: webapp(kM6ServerNS)
	, mConfig(inConfig)
{
	string docroot = "docroot";
	zx::element* e = mConfig->find_first("docroot");
	if (e != nullptr)
		docroot = e->content();
	set_docroot(docroot);
	
	mount("",				boost::bind(&M6Server::handle_welcome, this, _1, _2, _3));
	mount("entry",			boost::bind(&M6Server::handle_entry, this, _1, _2, _3));
	mount("search",			boost::bind(&M6Server::handle_search, this, _1, _2, _3));
	mount("scripts",		boost::bind(&M6Server::handle_file, this, _1, _2, _3));
	mount("css",			boost::bind(&M6Server::handle_file, this, _1, _2, _3));
	mount("man",			boost::bind(&M6Server::handle_file, this, _1, _2, _3));
	mount("images",			boost::bind(&M6Server::handle_file, this, _1, _2, _3));
	mount("favicon.ico",	boost::bind(&M6Server::handle_file, this, _1, _2, _3));

	add_processor("entry",	boost::bind(&M6Server::process_mrs_entry, this, _1, _2, _3));
	add_processor("link",	boost::bind(&M6Server::process_mrs_link, this, _1, _2, _3));
	add_processor("redirect",
							boost::bind(&M6Server::process_mrs_redirect, this, _1, _2, _3));

	LoadAllDatabanks();
}

void M6Server::init_scope(el::scope& scope)
{
	webapp::init_scope(scope);

	vector<el::object> databanks;
	foreach (M6LoadedDatabank& db, mLoadedDatabanks)
	{
		el::object databank;
		databank["id"] = db.mID;
		databank["name"] = db.mName;
		databanks.push_back(databank);
	}
	scope.put("databanks", el::object(databanks));
}

// --------------------------------------------------------------------

void M6Server::handle_request(const zh::request& req, zh::reply& rep)
{
	try
	{
		zh::webapp::handle_request(req, rep);
	}
	catch (zh::status_type& s)
	{
		rep = zh::reply::stock_reply(s);
	}
	catch (std::exception& e)
	{
		el::scope scope(req);
		scope.put("errormsg", el::object(e.what()));

		create_reply_from_template("error.html", scope, rep);
	}
}

void M6Server::handle_welcome(const zh::request& request,
	const el::scope& scope, zh::reply& reply)
{
	create_reply_from_template("index.html", scope, reply);
}

void M6Server::handle_file(const zh::request& request,
	const el::scope& scope, zh::reply& reply)
{
	fs::path file = get_docroot() / scope["baseuri"].as<string>();
	
	webapp::handle_file(request, scope, reply);
	
	if (file.extension() == ".html")
		reply.set_content_type("application/xhtml+xml");
}

void M6Server::handle_entry(const zh::request& request, const el::scope& scope, zh::reply& reply)
{
	string db, nr;

	zh::parameter_map params;
	get_parameters(scope, params);

	db = params.get("db", "").as<string>();
	nr = params.get("nr", "").as<string>();

	if (db.empty() or nr.empty())		// shortcut
	{
		handle_welcome(request, scope, reply);
		return;
	}
	
	uint32 docNr = boost::lexical_cast<uint32>(nr);

	string q, rq, format;
	q = params.get("q", "").as<string>();
	rq = params.get("rq", "").as<string>();
	format = params.get("format", "entry").as<string>();
	
	el::scope sub(scope);
	sub.put("db", el::object(db));
//	sub.put("linkeddbs", el::object(GetLinkedDbs(db)));
	sub.put("nr", el::object(nr));
	if (not q.empty())
		sub.put("q", el::object(q));
	else if (not rq.empty())
	{
		q = rq;
		sub.put("redirect", el::object(rq));
		sub.put("q", el::object(rq));
	}
	sub.put("format", el::object(format));

//	vector<string> linked;
//	GetLinkedDbs(db, id, linked);
//	if (not linked.empty())
//	{
//		vector<el::object> links;
//		foreach (string& l, linked)
//			links.push_back(el::object(l));
//		sub.put("links", el::object(links));
//	}

	M6Databank* mdb = Load(db);
	zx::element* dbConfig = M6Config::Instance().LoadConfig(db);
//	unique_ptr<M6Document> document(mdb->Fetch(docNr));

	// first stuff some data into scope
	
	el::object databank;
	databank["id"] = db;
	if (zx::element* name = dbConfig->find_first("name"))
		databank["name"] = name->content();
	else
		databank["name"] = db;
	if (zx::element* info = dbConfig->find_first("info"))
		databank["url"] = info->content();
	
//#ifndef NO_BLAST
//	databank["blastable"] = mNoBlast.count(db) == 0 and mdb->GetBlastDbCount() > 0;
//#endif
	sub.put("databank", databank);
//	sub.put("title", document->GetAttribute("title"));
	
	fs::ifstream data(get_docroot() / "entry.html");
	zx::document doc;
	doc.set_preserve_cdata(true);
	doc.read(data);

	zx::element* root = doc.child();

	try
	{
		process_xml(root, sub, "/");	
		
		try
		{
			M6Iterator* filter;
			vector<string> terms;
			ParseQuery(*mdb, q, false, terms, filter);
			delete filter;
			
			if (not terms.empty())
			{
				string pattern = ba::join(terms, "|");
				
				if (uc::contains_han(pattern))
					pattern = string("(") + pattern + ")";
				else
					pattern = string("\\b(") + pattern + ")\\b";
				
				boost::regex re(pattern, boost::regex_constants::icase);
				highlight_query_terms(root, re);
			}
		}
		catch (...) {}
		
		reply.set_content(doc);
	}
	catch (M6Redirect& redirect)
	{
		create_redirect(redirect.db, redirect.nr, "", false, request, reply);
	}
}

void M6Server::highlight_query_terms(zx::element* node, boost::regex& expr)
{
	foreach (zx::element* e, *node)
		highlight_query_terms(e, expr);

	foreach (zx::node* n, node->nodes())
	{
		zx::text* text = dynamic_cast<zx::text*>(n);
		
		if (text == nullptr)
			continue;

		for (;;)
		{
			boost::smatch m;

			// somehow boost::regex_search works incorrectly with a const std::string...
#if defined(_MSC_VER)
			string s = text->str();
#else
			const string& s = text->str();
#endif
			if (not boost::regex_search(s, m, expr) or not m[0].matched or m[0].length() == 0)
				break;

			// split the text
			node->insert(text, new zx::text(m.prefix()));
			
			zx::element* span = new zx::element("span");
			span->add_text(m[0]);
			span->set_attribute("class", "highlight");
			node->insert(text, span);
			
			text->str(m.suffix());
		}
	}
}

void M6Server::handle_search(const zh::request& request,
	const el::scope& scope, zh::reply& reply)
{
	string q, db, id, firstDb;
	uint32 page, hitCount = 0, firstDocNr = 0;
	
	zh::parameter_map params;
	get_parameters(scope, params);

	q = params.get("q", "").as<string>();
	db = params.get("db", "").as<string>();
	page = params.get("page", 1).as<uint32>();

	if (page < 1)
		page = 1;

	el::scope sub(scope);
	sub.put("page", el::object(page));
	sub.put("db", el::object(db));
	sub.put("q", el::object(q));

	// store the query terms
	vector<string> queryTerms;

	if (db.empty() or q.empty() or (db == "all" and q == "*"))
		handle_welcome(request, scope, reply);
	else if (db == "all")
	{
		uint32 hits_per_page = params.get("count", 3).as<uint32>();
		if (hits_per_page > 5)
			hits_per_page = 5;
		
//		sub.put("linkeddbs", el::object(GetLinkedDbs(db)));
		sub.put("show", el::object(hits_per_page));
	
		string hitDb = db;
		bool ranked = false;

		vector<el::object> databanks;
		
		foreach (M6LoadedDatabank& db, mLoadedDatabanks)
		{
//			if (mIgnoreInAll.count(dbi.GetID()))
//				continue;

			int32 maxresultcount = hits_per_page;

			el::object databank;
			databank["id"] = db.mID;
			databank["name"] = db.mName;
			
			unique_ptr<M6Iterator> rset;
			
			M6Iterator* filter;
			ParseQuery(*db.mDatabank, q, true, queryTerms, filter);
			if (queryTerms.empty())
				rset.reset(filter);
			else
				rset.reset(db.mDatabank->Find(queryTerms, filter, true, maxresultcount));

			if (not rset)
				continue;
			
			if (firstDb.empty())
				firstDb = db.mID;

			ranked = rset->IsRanked();
	
			uint32 docNr, nr = 1;
			
			vector<el::object> hits;
			sub.put("first", el::object(nr));
			
			float score = 0;
			while (maxresultcount-- > 0 and rset->Next(docNr, score))
			{
				if (firstDocNr == 0)
					firstDocNr = docNr;

				unique_ptr<M6Document> doc(db.mDatabank->Fetch(docNr));
				
				el::object hit;
				hit["nr"] = nr;
				hit["docNr"] = docNr;
				hit["id"] = doc->GetAttribute("id");
				hit["title"] = doc->GetAttribute("title");
				hit["score"] = static_cast<uint16>(score * 100);
				
//				vector<string> linked;
//				GetLinkedDbs(db, id, linked);
//				if (not linked.empty())
//				{
//					vector<el::object> links;
//					foreach (string& l, linked)
//						links.push_back(el::object(l));
//					hit["links"] = links;
//				}
				
				hits.push_back(hit);
				++nr;
			}
			
			if (not hits.empty())
			{
				databank["hits"] = hits;
				databank["hitCount"] = rset->GetCount();
				databanks.push_back(databank);
				
				hitCount += rset->GetCount();
			}
		}

		if (not databanks.empty())
		{
			sub.put("ranked", el::object(ranked));
			sub.put("hit-databanks", el::object(databanks));
		}
	}
	else
	{
		uint32 hits_per_page = params.get("show", 15).as<uint32>();
		if (hits_per_page > 100)
			hits_per_page = 100;
		
		sub.put("page", el::object(page));
		sub.put("db", el::object(db));
//		sub.put("linkeddbs", el::object(GetLinkedDbs(db)));
		sub.put("q", el::object(q));
		sub.put("show", el::object(hits_per_page));

		int32 maxresultcount = hits_per_page, resultoffset = 0;
		
		if (page > 1)
			resultoffset = (page - 1) * maxresultcount;
		
		M6Databank* mdb = Load(db);

		M6Iterator* filter;
		ParseQuery(*mdb, q, true, queryTerms, filter);

		unique_ptr<M6Iterator> rset;
		if (queryTerms.empty())
			rset.reset(filter);
		else
			rset.reset(mdb->Find(queryTerms, filter, true, maxresultcount));

//		unique_ptr<M6Iterator> rset(mdb->Find(q, true, page * maxresultcount));
		if (not rset)
		{
			rset.reset(mdb->Find(q, false, page * maxresultcount));
			sub.put("relaxed", el::object(true));
		}

		if (rset)
		{
			uint32 docNr, nr = 1;
			float score;
			while (resultoffset-- > 0 and rset->Next(docNr, score))
				++nr;
			
			vector<el::object> hits;
			sub.put("first", el::object(nr));
			
			while (maxresultcount-- > 0 and rset->Next(docNr, score))
			{
				if (firstDocNr == 0)
					firstDocNr = docNr;
				unique_ptr<M6Document> doc(mdb->Fetch(docNr));

				el::object hit;
				hit["nr"] = nr;
				hit["docNr"] = docNr;
				hit["id"] = id = doc->GetAttribute("id");
				hit["title"] = doc->GetAttribute("title");
				hit["score"] = tr1::trunc(score * 100);

//				vector<string> linked;
//				GetLinkedDbs(db, id, linked);
//				if (not linked.empty())
//				{
//					vector<el::object> links;
//					foreach (string& l, linked)
//						links.push_back(el::object(l));
//					hit["links"] = links;
//				}
				
				hits.push_back(hit);
	
				++nr;
			}
			
			uint32 count = rset->GetCount();
			
			sub.put("hits", el::object(hits));
			sub.put("hitCount", el::object(count));
			sub.put("lastPage", el::object(((count - 1) / hits_per_page) + 1));
			sub.put("last", el::object(nr - 1));
			sub.put("ranked", rset->IsRanked());

			hitCount += count;
		}
	}

	// OK, now if we only have one hit, we might as well show it directly of course...
	if (hitCount == 1)
		create_redirect(firstDb, firstDocNr, q, true, request, reply);
	else
	{
		// add some spelling suggestions
		sort(queryTerms.begin(), queryTerms.end());
		queryTerms.erase(unique(queryTerms.begin(), queryTerms.end()), queryTerms.end());
		
		if (not queryTerms.empty())
		{
			vector<el::object> suggestions;
			
			foreach (string& term, queryTerms)
			{
				try
				{
					boost::regex re(string("\\b") + term + "\\b");
	
					vector<string> s;
					SpellCheck(db, term, s);
					if (s.empty())
						continue;
					
					vector<el::object> alternatives;
					foreach (string& at, s)
					{
						el::object alt;
						alt["term"] = at;
	
						// construct new query, with the term replaced by the alternative
						ostringstream t;
						ostream_iterator<char, char> oi(t);
						boost::regex_replace(oi, q.begin(), q.end(), re, at,
							boost::match_default | boost::format_all);
	
						alt["q"] = t.str();
						alternatives.push_back(alt);
					}
					
					el::object so;
					so["term"] = term;
					so["alternatives"] = alternatives;
					
					suggestions.push_back(so);
				}
				catch (...) {}	// silently ignore errors
			}
				
			if (not suggestions.empty())
				sub.put("suggestions", el::object(suggestions));
		}

		create_reply_from_template(db == "all" ? "results-for-all.html" : "results.html",
			sub, reply);
	}
}

// --------------------------------------------------------------------

void M6Server::process_mrs_entry(zx::element* node, const el::scope& scope, fs::path dir)
{
	// evaluate attributes first
	foreach (zx::attribute& a, boost::iterator_range<zx::element::attribute_iterator>(node->attr_begin(), node->attr_end()))
	{
		string s = a.value();
		if (process_el(scope, s))
			a.value(s);
	}

	// fetch the entry
	string db = node->get_attribute("db");
	string nr = node->get_attribute("nr");
	string format = node->get_attribute("format");
	
	M6Databank* mdb = Load(db);
	if (mdb == nullptr)
		THROW(("databank not loaded"));
	unique_ptr<M6Document> doc(mdb->Fetch(boost::lexical_cast<uint32>(nr)));
	
	zx::node* replacement = nullptr;
	
	if (format == "title")
		replacement = new zx::text(doc->GetAttribute("title"));
	else // if (format == "plain")
	{
		zx::element* pre = new zx::element("pre");
		pre->add_text(doc->GetText());
		replacement = pre;
	}
//#ifndef NO_BLAST
//	else if (format == "fasta")
//	{
//		zx::element* pre = new zx::element("pre");
//		pre->add_text(doc->GetText());
//		replacement = pre;
//	}
//#endif
//	else	// fall back to html
//	{
//		string entry = WFormat::Instance()->Format(mDbTable, db, id, "html");
//
//		try
//		{
//			// turn into XML... this is tricky
//			zx::document doc;
//			doc.set_preserve_cdata(true);
//			doc.read(entry);
//		
//			replacement = doc.child();
//			doc.root()->remove(replacement);
//		}
//		catch (exception& ex)
//		{
//			// parsing the pretty printed entry went wrong, try
//			// to display the plain text along with a warning
//			
//			zx::element* e = new zx::element("div");
//			
//			zx::element* m = new zx::element("div");
//			m->set_attribute("class", "format-error");
//			m->add_text("Error formatting entry: ");
//			m->add_text(ex.what());
//			e->push_back(m);
//			e->add_text("\n");
//			zx::comment* cmt = new zx::comment(entry);
//			e->push_back(cmt);
//			e->add_text("\n");
//			
//			zx::element* pre = new zx::element("pre");
//			pre->set_attribute("class", "format-error");
//			pre->add_text(mrsDb->GetDocument(id));
//			e->push_back(pre);
//			
//			replacement = e;
//		}
//	}
	
	if (replacement != nullptr)
	{
		zx::container* parent = node->parent();
		assert(parent);
		parent->insert(node, replacement);

		process_xml(replacement, scope, dir);
	}
}

void M6Server::process_mrs_link(zx::element* node, const el::scope& scope, fs::path dir)
{
	string db = node->get_attribute("db");				process_el(scope, db);
	string nr = node->get_attribute("nr");				process_el(scope, nr);
	string id = node->get_attribute("id");				process_el(scope, id);
	string ix = node->get_attribute("index");			process_el(scope, ix);
	string an = node->get_attribute("anchor");			process_el(scope, an);
	string title = node->get_attribute("title");		process_el(scope, title);
	string q = node->get_attribute("q");				process_el(scope, q);

	bool exists = false;
	
	if (nr.empty())
	{
		try
		{
			M6Databank* mdb = Load(db);
			if (mdb != nullptr)
			{
				unique_ptr<M6Iterator> rset(mdb->Find(ix, id, false));
				
				uint32 docNr, docNr2; float rank;
				if (rset and rset->Next(docNr, rank))
				{
					exists = true;
					if (not rset->Next(docNr2, rank))
						nr = boost::lexical_cast<string>(docNr);
				}
			}
		}
		catch (...) {}
	}
	
	zx::element* a = new zx::element("a");
	
	if (not nr.empty())
		a->set_attribute("href",
			(boost::format("entry?db=%1%&nr=%2%%3%%4%")
				% zh::encode_url(db)
				% zh::encode_url(nr)
				% (q.empty() ? "" : ("&q=" + zh::encode_url(q)).c_str())
				% (an.empty() ? "" : (string("#") + zh::encode_url(an)).c_str())
			).str());
	else
	{
		a->set_attribute("href",
			(boost::format("link?db=%1%&ix=%2%&id=%3%%4%%5%")
				% zh::encode_url(db)
				% zh::encode_url(ix)
				% zh::encode_url(id)
				% (q.empty() ? "" : ("&q=" + zh::encode_url(q)).c_str())
				% (an.empty() ? "" : (string("#") + zh::encode_url(an)).c_str())
			).str());
	
		if (not exists)
			a->set_attribute("class", "not-found");
	}

	if (not title.empty())
		a->set_attribute("title", title);

	zx::container* parent = node->parent();
	assert(parent);
	parent->insert(node, a);
	
	foreach (zx::node* c, node->nodes())
	{
		zx::node* clone = c->clone();
		a->push_back(clone);
		process_xml(clone, scope, dir);
	}
}

void M6Server::process_mrs_redirect(zx::element* node, const el::scope& scope, fs::path dir)
{
}

// --------------------------------------------------------------------

void M6Server::create_redirect(const string& databank, uint32 inDocNr,
	const string& q, bool redirectForQuery, const zh::request& request, zh::reply& reply)
{
	string host = request.local_address;
	foreach (const zh::header& h, request.headers)
	{
		if (ba::iequals(h.name, "Host"))
		{
			host = h.value;
			break;
		}
	}

	// for some weird reason, local_port is sometimes 0 (bug in asio?)
	if (request.local_port != 80 and request.local_port != 0 and host.find(':') == string::npos)
	{
		host += ':';
		host += boost::lexical_cast<string>(request.local_port);
	}

	M6Databank* mdb = Load(databank);
	if (mdb == nullptr)
	{
		// ouch, nothing found... fall back to a lame page that says so
		// (this is an error, actually)
		
		el::scope scope(request);
		create_reply_from_template("results.html", scope, reply);
	}
	else
	{
		string location =
			(boost::format("http://%1%/entry?db=%2%&nr=%3%&%4%=%5%")
				% host
				% zh::encode_url(databank)
				% inDocNr
				% (redirectForQuery ? "rq" : "q")
				% zh::encode_url(q)
			).str();

		reply = zh::reply::redirect(location);
	}
}

// --------------------------------------------------------------------

void M6Server::LoadAllDatabanks()
{
	foreach (zx::element* db, mConfig->find("dbs/db"))
	{
		string databank = db->content();

		zx::element* config = M6Config::Instance().LoadConfig(databank);
		if (not config)
		{
			if (VERBOSE)
				cerr << "unknown databank " << databank << endl;
			continue;
		}
		
		zx::element* file = config->find_first("file");
		if (file == nullptr)
		{
			if (VERBOSE)
				cerr << "file not specified for databank " << databank << endl;
			continue;
		}

		fs::path path = file->content();
		if (not fs::exists(path))
		{
			if (VERBOSE)
				cerr << "databank " << databank << " not available" << endl;
			continue;
		}
		
		try
		{
			string name = databank;
			if (zx::element* n = db->find_first("name"))
				name = n->content();
			
			M6LoadedDatabank db =
			{
				new M6Databank(path, eReadOnly),
				databank,
				name
			};

			mLoadedDatabanks.push_back(db);
		}
		catch (exception& e)
		{
			cerr << "Error loading databank " << databank << endl
				 << " >> " << e.what() << endl;
		}
	}
}

M6Databank* M6Server::Load(const std::string& inDatabank)
{
	M6Databank* result = nullptr;

	foreach (M6LoadedDatabank& db, mLoadedDatabanks)
	{
		if (db.mID == inDatabank)
		{
			result = db.mDatabank;
			break;
		}
	}
	
	return result;
}

void M6Server::SpellCheck(const string& inDatabank, const string& inTerm,
	vector<string>& outSuggestions)
{
	if (inDatabank == "all")
		;
	else
	{
		M6Databank* db = Load(inDatabank);
		db->SuggestCorrection(inTerm, outSuggestions);
	}
}

