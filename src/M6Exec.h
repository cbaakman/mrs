#pragma once

#include <vector>
#include <string>

#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/categories.hpp>
#include <boost/iostreams/read.hpp>

int ForkExec(std::vector<const char*>& args, double maxRunTime,
	const std::string& in, std::string& out, std::string& err);

class M6Process : public boost::iostreams::source
{
  public:
	
	typedef char char_type;
	typedef boost::iostreams::source_tag category;

							M6Process(const std::string& inCommand,
								std::istream& inRawData);
							M6Process(const M6Process&);
	M6Process&				operator=(const M6Process&);
							~M6Process();

	std::streamsize			read(char* s, std::streamsize n);

  private:

	struct M6ProcessImpl*	mImpl;
};

