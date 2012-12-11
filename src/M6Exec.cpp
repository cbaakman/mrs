#include "M6Lib.h"

#include <iostream>

#if defined(_MSC_VER)
#include <Windows.h>
#include <time.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <fcntl.h>
#endif

#include <boost/filesystem/operations.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/thread.hpp>
#include <boost/thread/condition.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>

#include "M6Exec.h"
#include "M6Error.h"
#include "M6Server.h"

using namespace std;
namespace fs = boost::filesystem;
namespace po = boost::program_options;
namespace ba = boost::algorithm;

double system_time()
{
#if defined(_MSC_VER)
	static double sDiff = -1.0;

	FILETIME tm;
	ULARGE_INTEGER li;
	
	if (sDiff == -1.0)
	{
		SYSTEMTIME st = { 0 };

		st.wDay = 1;
		st.wMonth = 1;
		st.wYear = 1970;

		if (not ::SystemTimeToFileTime(&st, &tm))
			THROW(("Getting system tile failed"));

		li.LowPart = tm.dwLowDateTime;
		li.HighPart = tm.dwHighDateTime;
		
		// Prevent Ping Pong comment. VC cannot convert UNSIGNED int64 to double. SIGNED is ok. (No more long)
		sDiff = static_cast<double> (li.QuadPart);
		sDiff /= 1e7;
	}	
	
	::GetSystemTimeAsFileTime(&tm);
	
	li.LowPart = tm.dwLowDateTime;
	li.HighPart = tm.dwHighDateTime;
	
	double result = static_cast<double> (li.QuadPart);
	result /= 1e7;
	return result - sDiff;
#else
	struct timeval tv;
	
	gettimeofday(&tv, nullptr);
	
	return tv.tv_sec + tv.tv_usec / 1e6;
#endif
}

#if defined(_MSC_VER)

int ForkExec(vector<const char*>& args, double maxRunTime,
	const string& in, string& out, string& err)
{
	if (args.empty() or args.front() == nullptr)
		THROW(("No arguments to ForkExec"));

	string cmd;
	for (auto arg = args.begin(); arg != args.end() and *arg != nullptr; ++arg)
		cmd = cmd + '"' + *arg + "\" ";

	SECURITY_ATTRIBUTES sa = { 0 };
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = true;
	
	HANDLE hInputWriteTmp, hInputRead, hInputWrite;
	HANDLE hOutputReadTmp, hOutputRead, hOutputWrite;
	HANDLE hErrorReadTmp, hErrorRead, hErrorWrite;
	
	if (not CreatePipe(&hOutputReadTmp, &hOutputWrite, &sa, 0) or
		not CreatePipe(&hErrorReadTmp, &hErrorWrite, &sa, 0) or
		not CreatePipe(&hInputRead, &hInputWriteTmp, &sa, 0) or
		
		not DuplicateHandle(GetCurrentProcess(), hOutputReadTmp,
							GetCurrentProcess(), &hOutputRead,
							0, false, DUPLICATE_SAME_ACCESS) or
		not DuplicateHandle(GetCurrentProcess(), hErrorReadTmp,
							GetCurrentProcess(), &hErrorRead,
							0, false, DUPLICATE_SAME_ACCESS) or
		not DuplicateHandle(GetCurrentProcess(), hInputWriteTmp,
							GetCurrentProcess(), &hInputWrite,
							0, false, DUPLICATE_SAME_ACCESS) or
							
		not CloseHandle(hOutputReadTmp) or
		not CloseHandle(hErrorReadTmp) or
		not CloseHandle(hInputWriteTmp))
	{
		THROW(("Error creating pipes"));
	}

	boost::thread thread([&in, hInputWrite]() {
		DWORD w;
		if (not WriteFile(hInputWrite, in.c_str(), in.length(), &w, nullptr) or
			w != in.length())
		{
			cerr << "Error writing to pipe";
		}

		CloseHandle(hInputWrite);
	});
	
	STARTUPINFOA si = { 0 };
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = hInputRead;
	si.hStdOutput = hOutputWrite;
	si.hStdError = hErrorWrite;

	const char* cwd = nullptr;

	PROCESS_INFORMATION pi;
	bool running = CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()), nullptr, nullptr, true,
		CREATE_NEW_PROCESS_GROUP, nullptr, const_cast<char*>(cwd), &si, &pi);

	CloseHandle(hOutputWrite);
	CloseHandle(hErrorWrite);
	CloseHandle(hInputRead);

	if (running)
	{
		CloseHandle(pi.hThread);
	
		bool outDone = false, errDone = false;
		while (not (outDone and errDone))
		{
			char buffer[1024];
			DWORD rr, avail;
			
			if (not outDone)
			{
				if (not PeekNamedPipe(hOutputRead, nullptr, 0, nullptr, &avail, nullptr))
				{
					unsigned int err = GetLastError();
					if (err == ERROR_HANDLE_EOF or err == ERROR_BROKEN_PIPE)
						outDone = true;
				}
				else if (avail > 0 and ReadFile(hOutputRead, buffer, sizeof(buffer), &rr, nullptr))
					out.append(buffer, buffer + rr);
			}
	
			if (not errDone)
			{
				if (not PeekNamedPipe(hErrorRead, nullptr, 0, nullptr, &avail, nullptr))
				{
					unsigned int err = GetLastError();
					if (err == ERROR_HANDLE_EOF or err == ERROR_BROKEN_PIPE)
						errDone = true;
				}
				else if (avail > 0 and ReadFile(hErrorRead, buffer, sizeof(buffer), &rr, nullptr))
					err.append(buffer, buffer + rr);
			}
		}
		
		CloseHandle(pi.hProcess);
	}

	ba::replace_all(out, "\r\n", "\n");
	ba::replace_all(err, "\r\n", "\n");
	
	CloseHandle(hOutputRead);
	CloseHandle(hErrorRead);

	return 0;
}

struct M6ProcessImpl
{
					M6ProcessImpl(const string& inCommand, istream& inRawData);

	void			Reference();
	void			Release();

    streamsize		read(char* s, streamsize n);
  
	HANDLE			mOFD[2];

  private:
					~M6ProcessImpl();

	void			init();

	boost::thread	mThread;
	int32			mRefCount;
	string			mCommand;
	istream&		mRawData;
};

M6ProcessImpl::M6ProcessImpl(const string& inCommand, istream& inRawData)
	: mRefCount(1), mCommand(inCommand), mRawData(inRawData)
{
	mOFD[0] = mOFD[1] = nullptr;
}

M6ProcessImpl::~M6ProcessImpl()
{
	if (mThread.joinable())
	{
		mThread.interrupt();
		mThread.join();
	}
}

void M6ProcessImpl::Reference()
{
	++mRefCount;
}

void M6ProcessImpl::Release()
{
	if (--mRefCount == 0)
		delete this;
}

streamsize M6ProcessImpl::read(char* s, streamsize n)
{
	if (mOFD[0] == nullptr)
	{
		boost::mutex m;
		boost::condition c;
		boost::mutex::scoped_lock lock(m);
		
		mThread = boost::thread([this, &c]()
		{
			try
			{
				SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES) };
				sa.bInheritHandle = true;
				
				HANDLE ifd[2];
				
				::CreatePipe(&ifd[0], &ifd[1], &sa, 0);
				::SetHandleInformation(ifd[1], HANDLE_FLAG_INHERIT, 0); 
			
				::CreatePipe(&mOFD[0], &mOFD[1], &sa, 0);
				::SetHandleInformation(mOFD[0], HANDLE_FLAG_INHERIT, 0); 
			
				STARTUPINFOA si = { sizeof(STARTUPINFOA) };
				si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
				si.hStdInput = ifd[0];
				si.hStdOutput = mOFD[1];
		//		si.hStdError = mEFD[1];
			
				PROCESS_INFORMATION pi;
				if (not ::CreateProcessA(nullptr, const_cast<char*>(mCommand.c_str()), nullptr, nullptr, true,
						CREATE_NEW_PROCESS_GROUP, nullptr, /*const_cast<char*>(cwd.c_str())*/ nullptr, &si, &pi))
				{
					THROW(("Failed to launch %s (%d)", mCommand.c_str(), GetLastError()));
				}
			
				c.notify_one();
			
				for (;;)
				{
					char buffer[4096];
					mRawData.read(buffer, sizeof(buffer));
					if (mRawData.gcount() > 0)
					{
						DWORD rr;
						if (not ::WriteFile(ifd[1], buffer, mRawData.gcount(), &rr, nullptr))
							THROW(("WriteFile failed: %d", GetLastError()));
						continue;
					}
					
					if (mRawData.eof())
					{
						::CloseHandle(ifd[1]);
						break;
					}
				}
				
				::CloseHandle(mOFD[0]);
				if (pi.hProcess != nullptr)
				{
					::CloseHandle(pi.hProcess);
					::CloseHandle(pi.hThread);
				}
			}
			catch (exception& e)
			{
				cerr << "Process " << mCommand << " Failed: " << e.what() << endl;
				exit(1);
			}
		});

		c.wait(lock);
	}

	DWORD rr;

	if (not ::ReadFile(mOFD[0], s, n, &rr, nullptr))
		THROW(("Error reading data: %d", GetLastError()));

	return rr;
}

#else

int ForkExec(vector<const char*>& args, double maxRunTime,
	const string& stdin, string& stdout, string& stderr)
{
	if (args.empty() or args.front() == nullptr)
		THROW(("No arguments to ForkExec"));

	if (args.back() != nullptr)
		args.push_back(nullptr);
	
	if (not fs::exists(args.front()))
		THROW(("The executable '%s' does not seem to exist", args.front()));

	// ready to roll
	double startTime = system_time();

	int ifd[2], ofd[2], efd[2], err;
	
	err = pipe(ifd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));
	err = pipe(ofd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));
	err = pipe(efd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));
	
	boost::asio::io_service* ioService = nullptr;
	if (M6Server::Instance() != nullptr)
	{
		ioService = M6Server::Instance()->get_io_service();
		ioService->notify_fork(boost::asio::io_service::fork_prepare);
	}
	
	int pid = fork();
	
	if (pid == 0)	// the child
	{
		if (ioService != nullptr)
			ioService->notify_fork(boost::asio::io_service::fork_child);
		
		setpgid(0, 0);		// detach from the process group, create new

		signal(SIGCHLD, SIG_IGN);	// block child died signals

		dup2(ifd[0], STDIN_FILENO);
		close(ifd[0]);
		close(ifd[1]);

		dup2(ofd[1], STDOUT_FILENO);
		close(ofd[0]);
		close(ofd[1]);
						
		dup2(efd[1], STDERR_FILENO);
		close(efd[0]);
		close(efd[1]);

		const char* env[] = { nullptr };
		(void)execve(args.front(), const_cast<char* const*>(&args[0]), const_cast<char* const*>(env));
		exit(-1);
	}

	if (ioService != nullptr)
		ioService->notify_fork(boost::asio::io_service::fork_parent);
	
	if (pid == -1)
	{
		close(ifd[0]);
		close(ifd[1]);
		close(ofd[0]);
		close(ofd[1]);
		close(efd[0]);
		close(efd[1]);
		
		THROW(("fork failed: %s", strerror(errno)));
	}
	
	// make stdout and stderr non-blocking
	int flags;
	
	close(ifd[0]);
	
	if (stdin.empty())
		close(ifd[1]);
	else
	{
		flags = fcntl(ifd[1], F_GETFL, 0);
		fcntl(ifd[1], F_SETFL, flags | O_NONBLOCK);
	}

	close(ofd[1]);
	flags = fcntl(ofd[0], F_GETFL, 0);
	fcntl(ofd[0], F_SETFL, flags | O_NONBLOCK);

	close(efd[1]);
	flags = fcntl(efd[0], F_GETFL, 0);
	fcntl(efd[0], F_SETFL, flags | O_NONBLOCK);
	
	// OK, so now the executable is started and the pipes are set up
	// read from the pipes until done.
	
	bool errDone = false, outDone = false, killed = false;
	
	const char* in = stdin.c_str();
	size_t l_in = stdin.length();
	
	while (not errDone and not outDone)
	{
		if (l_in > 0)
		{
			size_t k = l_in;
			if (k > 1024)
				k = 1024;
			
			int r = write(ifd[1], in, k);
			if (r > 0)
				in += r, l_in -= r;
			else if (r < 0 and errno != EAGAIN)
				THROW(("Error writing to command %s", args.front()));
			
			if (l_in == 0)
				close(ifd[1]);
		}
		else
			usleep(100000);

		char buffer[1024];
		int r, n;
		
		n = 0;
		while (not outDone)
		{
			r = read(ofd[0], buffer, sizeof(buffer));
			
			if (r > 0)
				stdout.append(buffer, r);
			else if (r == 0 or errno != EAGAIN)
				outDone = true;
			else
				break;
		}
		
		n = 0;
		while (not errDone)
		{
			r = read(efd[0], buffer, sizeof(buffer));
			
			if (r > 0)
				stderr.append(buffer, r);
			else if (r == 0 and errno != EAGAIN)
				errDone = true;
			else
				break;
		}

		if (not errDone and not outDone and not killed and maxRunTime > 0 and startTime + maxRunTime < system_time())
		{
			kill(pid, SIGINT);

			int status = 0;
			waitpid(pid, &status, 0);

			THROW(("%s was killed since its runtime exceeded the limit of %d seconds", args.front(), maxRunTime));
		}
	}
	
	close(ofd[0]);
	close(efd[0]);
	if (l_in > 0)
		close(ifd[1]);
	
	// no zombies please, removed the WNOHANG. the forked application should really stop here.
	int status = 0;
	waitpid(pid, &status, 0);
	
	int result = -1;
	if (WIFEXITED(status))
		result = WEXITSTATUS(status);
	
	return result;
}

struct M6ProcessImpl
{
				M6ProcessImpl(const string& inCommand, istream& inRawData);

	void		Reference();
	void		Release();

	streamsize	read(char* s, streamsize n);

  private:
				~M6ProcessImpl();

	int32		mRefCount;
	int			mOFD;
	boost::thread
				mThread;
	string		mCommand;
	istream&	mRawData;
};

M6ProcessImpl::M6ProcessImpl(const string& inCommand, istream& inRawData)
	: mRefCount(1), mOFD(-1), mCommand(inCommand), mRawData(inRawData)
{
}

M6ProcessImpl::~M6ProcessImpl()
{
	assert(mRefCount == 0);
	if (mOFD != -1)
		::close(mOFD);
	
	if (mThread.joinable())
	{
		mThread.interrupt();
		mThread.join();
	}
}

void M6ProcessImpl::Reference()
{
	++mRefCount;
}

void M6ProcessImpl::Release()
{
	if (--mRefCount == 0)
		delete this;
}

streamsize M6ProcessImpl::read(char* s, streamsize n)
{
	if (mOFD == -1)
	{
		boost::mutex m;
		boost::condition c;
		boost::mutex::scoped_lock lock(m);
		
		mThread = boost::thread([this, &c]()
		{
			try
			{
				vector<string> argss = po::split_unix(this->mCommand);
				vector<const char*> args;
				foreach (string& arg, argss)
					args.push_back(arg.c_str());
				
				if (args.empty() or args.front() == nullptr)
					THROW(("No arguments to ForkExec"));
		
				if (args.back() != nullptr)
					args.push_back(nullptr);
				
				if (not fs::exists(args.front()))
					THROW(("The executable '%s' does not seem to exist", args.front()));
					
				if (VERBOSE)
					cerr << "Starting executable " << args.front() << endl;
			
				// ready to roll
				double startTime = system_time();
			
				int ifd[2], ofd[2], err;
				
				err = pipe(ifd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));
				err = pipe(ofd); if (err < 0) THROW(("Pipe error: %s", strerror(errno)));
				
				int pid = fork();
				
				if (pid == -1)
				{
					::close(ifd[0]);
					::close(ifd[1]);
					::close(ofd[0]);
					::close(ofd[1]);
					
					THROW(("fork failed: %s", strerror(errno)));
				}
				
				if (pid == 0)	// the child
				{
					setpgid(0, 0);		// detach from the process group, create new
			
					signal(SIGCHLD, SIG_IGN);	// block child died signals
			
					dup2(ifd[0], STDIN_FILENO);
					::close(ifd[0]);
					::close(ifd[1]);
			
					dup2(ofd[1], STDOUT_FILENO);
					::close(ofd[0]);
					::close(ofd[1]);
			
					const char* env[] = { nullptr };
					(void)execve(args.front(), const_cast<char* const*>(&args[0]), const_cast<char* const*>(env));
					exit(-1);
				}
			
				::close(ofd[1]);
				mOFD = ofd[0];
				
				c.notify_one();
				
				for (;;)
				{
					char buffer[4096];
					mRawData.read(buffer, sizeof(buffer));
					if (mRawData.gcount() > 0)
					{
						int r = ::write(ifd[1], buffer, mRawData.gcount());
						if (r <= 0)
							THROW(("WriteFile failed: %s", strerror(errno)));
						continue;
					}
					
					if (mRawData.eof())
					{
						::close(ifd[1]);
						break;
					}
				}
				
				if (pid > 0)
				{
					int status = 0;
					waitpid(pid, &status, 0);
					
					int result = -1;
					if (WIFEXITED(status))
						result = WEXITSTATUS(status);
				}
			}
			catch (exception& e)
			{
				cerr << "Process " << this->mCommand << " Failed: " << e.what() << endl;
				exit(1);
			}
		});

		c.wait(lock);
	}
			
	return ::read(mOFD, s, n);
}

#endif

M6Process::M6Process(const string& inCommand, istream& inRawData)
	: mImpl(new M6ProcessImpl(inCommand, inRawData))
{
}

M6Process::~M6Process()
{
	mImpl->Release();
}

M6Process::M6Process(const M6Process& rhs)
	: mImpl(rhs.mImpl)
{
	mImpl->Reference();
}

M6Process& M6Process::operator=(const M6Process& rhs)
{
	if (this != &rhs)
	{
		mImpl->Release();
		mImpl = rhs.mImpl;
		mImpl->Reference();
	}
	
	return *this;
}

streamsize M6Process::read(char* s, streamsize n)
{
	return mImpl->read(s, n);
}
