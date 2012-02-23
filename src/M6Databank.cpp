#include "M6Lib.h"

#include <set>
#include <iostream>

#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH
#include <boost/algorithm/string.hpp>

#include "M6Databank.h"
#include "M6Document.h"
#include "M6DocStore.h"
#include "M6Error.h"
#include "M6BitStream.h"
#include "M6Index.h"
#include "M6Progress.h"
#include "M6Queue.h"

using namespace std;
namespace fs = boost::filesystem;
namespace ba = boost::algorithm;

// --------------------------------------------------------------------

const uint32
	kM6WeightBitCount = 5,
	kM6MaxWeight = (1 << kM6WeightBitCount) - 1,
	kMaxIndexNr = 30;

class M6BatchIndexProcessor;

// --------------------------------------------------------------------

typedef boost::shared_ptr<M6BasicIndex> M6BasicIndexPtr;

class M6DatabankImpl
{
  public:
	typedef M6Queue<M6InputDocument*>	M6DocQueue;

					M6DatabankImpl(M6Databank& inDatabank, const fs::path& inPath, MOpenMode inMode);
	virtual			~M6DatabankImpl();

	void			StartBatchImport(M6Lexicon& inLexicon);
	void			CommitBatchImport();
		
	void			Store(M6Document* inDocument);
	M6Document*		Fetch(uint32 inDocNr);
	M6Document*		FindDocument(const string& inIndex, const string& inValue);
	M6Iterator*		Find(const string& inQuery, uint32 inReportLimit);
	
	M6DocStore&		GetDocStore()						{ return *mStore; }
	
	M6BasicIndexPtr	GetIndex(const string& inName, M6IndexType inType, bool inUnique);
	M6BasicIndexPtr	CreateIndex(const string& inName, M6IndexType inType, bool inUnique);
	M6BasicIndexPtr	GetAllTextIndex()					{ return mAllTextIndex; }
	
	fs::path		GetScratchDir() const				{ return mDbDirectory / "tmp"; }

	void			RecalculateDocumentWeights();
	void			Vacuum();

	void			Validate();
	void			DumpIndex(const string& inIndex, ostream& inStream);

	void			StoreThread();
	void			IndexThread();

  protected:

	struct M6IndexDesc
	{
							M6IndexDesc(const string& inName, M6BasicIndexPtr inIndex, M6IndexType inType, bool inUnique)
								: mName(inName), mIndex(inIndex), mType(inType), mUnique(inUnique) {}

		string				mName;
		M6BasicIndexPtr		mIndex;
		M6IndexType			mType;
		bool				mUnique;
	};
	typedef vector<M6IndexDesc>	M6IndexDescList;
	
	M6Databank&				mDatabank;
	fs::path				mDbDirectory;
	MOpenMode				mMode;
	M6DocStore*				mStore;
	M6BatchIndexProcessor*	mBatch;
	M6IndexDescList			mIndices;
	M6BasicIndexPtr			mAllTextIndex;
	vector<float>			mDocWeights;
	M6DocQueue				mStoreQueue, mIndexQueue;
	boost::thread			mStoreThread, mIndexThread;
};

// --------------------------------------------------------------------

class M6FullTextIx
{
  public:
					M6FullTextIx(const fs::path& inScratch);
	virtual			~M6FullTextIx();
	
	void			SetUsesInDocLocation(uint32 inIndexNr)		{ mDocLocationIxMap |= (1 << inIndexNr); }
	bool			UsesInDocLocation(uint32 inIndexNr) const	{ return mDocLocationIxMap & (1 << inIndexNr); }
	uint32			GetDocLocationIxMap() const					{ return mDocLocationIxMap; }

	void			SetExcludeInFullText(uint32 inIndexNr)		{ mFullTextIxMap |= (1 << inIndexNr); }
	bool			ExcludesInFullText(uint32 inIndexNr) const	{ return mFullTextIxMap & (1 << inIndexNr); }
	uint32			GetFullTextIxMap() const					{ return mFullTextIxMap; }

	void			AddWord(uint8 inIndex, uint32 inWord);
	void			FlushDoc(uint32 inDocNr);

	struct M6BufferEntry
	{
		M6OBitStream	idl;
		uint8			ix;
		uint8			weight;	// for weighted keys
		uint32			term;
		uint32			doc;
		
		bool			operator<(const M6BufferEntry& inOther) const
							{ return term < inOther.term or
									(term == inOther.term and doc < inOther.doc); }
	};

	void			PushEntry(const M6BufferEntry& inEntry);
	int64			Finish();
	bool			NextEntry(M6BufferEntry& outEntry);
	
	fs::path		GetScratchDir() const						{ return mScratchDir; }

  private:
	
	typedef vector<uint32>		DocLoc;

	struct DocWord
	{
		uint32		word;
		uint32		index;
		uint32		freq;
		DocLoc		loc;
		
		bool		operator<(const DocWord& inOther) const
						{ return word < inOther.word or (word == inOther.word and index < inOther.index); }
	};

	typedef set<DocWord> DocWords;

	// the number of buffer entries is one of the most important
	// variables affecting indexing speed and memory consumption.
	// The value chosen here seems to be a reasonable tradeoff.
	enum {
		kM6BufferEntryCount = 800000
//		kM6BufferEntryCount = 8000
	};
	
	struct M6EntryRun
	{
		uint32			mCount;
		M6BufferEntry	mEntries[kM6BufferEntryCount];
	};
	
	typedef M6Queue<M6EntryRun*>	M6EntryRunQueue;

	void			FlushEntryRuns();
	
	struct M6BufferEntryIterator
	{
						M6BufferEntryIterator(M6File& inFile, int64 inOffset, uint32 inCount,
							uint32 inFirstDoc, uint32 inIDLIxMap)
							: mBits(inFile, inOffset), mCount(inCount), mFirstDoc(inFirstDoc), mIDLIxMap(inIDLIxMap)
							, mTerm(1), mDoc(inFirstDoc) {}
		
		bool			Next();
		
		M6IBitStream	mBits;
		uint32			mCount;
		uint32			mFirstDoc;
		uint32			mIDLIxMap;
		uint32			mTerm;
		uint32			mDoc;
		M6BufferEntry	mEntry;
	};
	
	struct CompareEntryIterator
	{
		bool operator()(const M6BufferEntryIterator* a, const M6BufferEntryIterator* b) const
						{ return not (a->mEntry < b->mEntry); }
	};
	
	typedef vector<M6BufferEntryIterator*>	M6EntryQueue;
	
	DocWords		mDocWords;
	uint32			mDocLocationIxMap, mFullTextIxMap;
	uint32			mDocWordLocation;
	fs::path		mScratchDir;

	M6File			mEntryBuffer;
	M6EntryRun*		mEntryRun;
	M6EntryRunQueue	mEntryRunQueue;
	boost::thread	mEntryRunThread;
	M6EntryQueue	mEntryQueue;
	int64			mEntryCount;
};

ostream& operator<<(ostream& os, const M6FullTextIx::M6BufferEntry& e)
{
	os << e.term << '\t'
	   << e.doc << '\t'
	   << uint32(e.ix) << '\t'
	   << uint32(e.weight);
	return os;
}

M6FullTextIx::M6FullTextIx(const fs::path& inScratchUrl)
	: mDocLocationIxMap(0), mFullTextIxMap(0)
	, mDocWordLocation(1)
	, mScratchDir(inScratchUrl)
	, mEntryBuffer(mScratchDir / "fulltext", eReadWrite)
	, mEntryRun(nullptr)
	, mEntryRunThread(boost::bind(&M6FullTextIx::FlushEntryRuns, this))
	, mEntryCount(0)
{
}

M6FullTextIx::~M6FullTextIx()
{
	delete mEntryRun;
	if (mEntryRunThread.joinable())
	{
		mEntryRunQueue.Put(nullptr);
		mEntryRunThread.join();
		
		foreach (M6BufferEntryIterator* iter, mEntryQueue)
			delete iter;
	}
}

void M6FullTextIx::AddWord(uint8 inIndex, uint32 inWord)
{
	++mDocWordLocation;	// always increment, no matter if we do not add the word
	
	if (inWord > 0)
	{
		if (inIndex > kMaxIndexNr)
			THROW(("Too many full text indices"));
		
		DocWord w = { inWord, inIndex, 1 };
		
		DocWords::iterator i = mDocWords.find(w);
		if (i != mDocWords.end())
			const_cast<DocWord&>(*i).freq += 1;
		else
			i = mDocWords.insert(w).first;
		
		if (UsesInDocLocation(inIndex))
		{
			DocWord& dw = const_cast<DocWord&>(*i);
			dw.loc.push_back(mDocWordLocation);
		}
	}
}

void M6FullTextIx::FlushDoc(uint32 inDoc)
{
	// normalize the frequencies.
	uint32 maxFreq = 1;
	
	for (DocWords::iterator w = mDocWords.begin(); w != mDocWords.end(); ++w)
		if (w->freq > maxFreq)
			maxFreq = w->freq;

	for (DocWords::iterator w = mDocWords.begin(); w != mDocWords.end(); ++w)
	{
		if (w->freq == 0)
			continue;
		
		M6BufferEntry e = {};
		
		e.term = w->word;
		e.doc = inDoc;
		e.ix = w->index;

		e.weight = (w->freq * kM6MaxWeight) / maxFreq;
		if (e.weight < 1)
			e.weight = 1;
		
		if (UsesInDocLocation(w->index))
			WriteArray(e.idl, w->loc);

		PushEntry(e);
	}
	
	mDocWords.clear();
	mDocWordLocation = 1;
}

void M6FullTextIx::PushEntry(const M6BufferEntry& inEntry)
{
	if (mEntryRun != nullptr and mEntryRun->mCount >= kM6BufferEntryCount)
	{
		mEntryRunQueue.Put(mEntryRun);
		mEntryRun = nullptr;
	}
	
	if (mEntryRun == nullptr)
	{
		mEntryRun = new M6EntryRun;
		mEntryRun->mCount = 0;
	}
	
	mEntryRun->mEntries[mEntryRun->mCount] = inEntry;
	++mEntryRun->mCount;
	++mEntryCount;
}

void M6FullTextIx::FlushEntryRuns()
{
	for (;;)
	{
		M6EntryRun* run = mEntryRunQueue.Get();
		if (run == nullptr)
			break;
		
		M6OBitStream bits(mEntryBuffer);
		
		M6BufferEntry* entries = run->mEntries;
		uint32 count = run->mCount;
		
		uint32 t = 1;
		uint32 firstDoc = entries[0].doc;	// the first doc in this run
		uint32 d = firstDoc;
		int32 ix = -1;
		
		// create an iterator now that we have all the info
		mEntryQueue.push_back(new M6BufferEntryIterator(mEntryBuffer, mEntryBuffer.Size(),
			count, firstDoc, mDocLocationIxMap));
		
		stable_sort(entries, entries + count);
	
		for (uint32 i = 0; i < count; ++i)
		{
			assert(entries[i].term > t or entries[i].doc > d or entries[i].ix > ix);
	
			if (entries[i].term > t)
				d = firstDoc;
	
			WriteGamma(bits, entries[i].term - t + 1);
			WriteGamma(bits, entries[i].doc - d + 1);
			WriteGamma(bits, entries[i].ix + 1);
			WriteBinary(bits, kM6WeightBitCount, entries[i].weight);
	
			if (mDocLocationIxMap & (1 << entries[i].ix))
				WriteBits(bits, entries[i].idl);
	
			t = entries[i].term;
			d = entries[i].doc;
			ix = entries[i].ix;
		}
		
		bits.Sync();

		delete run;
	}
}

int64 M6FullTextIx::Finish()
{
	// flush the runs and stop the thread
	if (mEntryRun != nullptr)
	{
		mEntryRunQueue.Put(mEntryRun);
		mEntryRun = nullptr;
	}

	mEntryRunQueue.Put(nullptr);
	mEntryRunThread.join();
	
	// setup the input queue
	vector<M6BufferEntryIterator*> iterators;
	swap(mEntryQueue, iterators);
	mEntryQueue.reserve(iterators.size());
	
	foreach (M6BufferEntryIterator* iter, iterators)
	{
		if (iter->Next())
		{
			mEntryQueue.push_back(iter);
			push_heap(mEntryQueue.begin(), mEntryQueue.end(), CompareEntryIterator());
		}
		else
			delete iter;
	}
	return mEntryCount;
}

bool M6FullTextIx::NextEntry(M6BufferEntry& outEntry)
{
	bool result = false;
	
	if (not mEntryQueue.empty())
	{
		pop_heap(mEntryQueue.begin(), mEntryQueue.end(), CompareEntryIterator());
		M6BufferEntryIterator* iter = mEntryQueue.back();
		
		outEntry = iter->mEntry;
		
		if (iter->Next())
			push_heap(mEntryQueue.begin(), mEntryQueue.end(), CompareEntryIterator());
		else
		{
			mEntryQueue.erase(mEntryQueue.end() - 1);
			delete iter;
		}
		
		result = true;
	}
	
	return result;
}

bool M6FullTextIx::M6BufferEntryIterator::Next()
{
	bool result = false;
	if (mCount > 0)
	{
		uint32 delta;
		ReadGamma(mBits, delta);
		delta -= 1;
	
		if (delta != 0)
		{
			mTerm += delta;
			mDoc = mFirstDoc;
		}
		
		mEntry.term = mTerm;
		ReadGamma(mBits, delta);
		delta -= 1;
		mDoc += delta;
		mEntry.doc = mDoc;
		ReadGamma(mBits, mEntry.ix);
		mEntry.ix -= 1;
		ReadBinary(mBits, kM6WeightBitCount, mEntry.weight);
		
		if (mIDLIxMap & (1 << mEntry.ix))
			ReadBits(mBits, mEntry.idl);
	
		--mCount;
		result = true;
	}
	return result;
}

// --------------------------------------------------------------------

class M6BasicIx
{
  public:
	
	struct FlushedTerm
	{
		string			mTerm;
		vector<uint32>	mDocs;
	};
	typedef M6Queue<FlushedTerm*>	M6FlushQueue;

					M6BasicIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
						const string& inName, uint8 inIndexNr);
	virtual 		~M6BasicIx();
	
	void			AddWord(uint32 inWord);
	void			SetDbDocCount(uint32 inDbDocCount);
	void			AddDocTerm(uint32 inDoc, uint32 inTerm, uint8 inFrequency, M6OBitStream& inIDL);

	bool			Empty() const							{ return mLastDoc == 0; }
	uint8			GetIxNr() const							{ return mIndexNr; }
	string			GetName() const							{ return mName; }

  protected:
	
	virtual void	AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL);
	virtual void	FlushTerm(uint32 inTerm, uint32 inDocCount);
	
	virtual void	FlushTerm(FlushedTerm* inTermData) = 0;
	void			FlushThread();

	string			mName;
	M6FullTextIx&	mFullTextIndex;
	M6Lexicon&		mLexicon;
	uint8			mIndexNr;
	
	// data for the second pass
	uint32			mLastDoc;
	uint32			mLastTerm;
	M6OBitStream	mBits;
	uint32			mDocCount;
	uint32			mDbDocCount;
	
	M6FlushQueue	mFlushQueue;
	boost::thread	mFlushThread;
};

// --------------------------------------------------------------------
//	
//

M6BasicIx::M6BasicIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
		const string& inName, uint8 inIndexNr)
	: mName(inName)
	, mFullTextIndex(inFullTextIndex)
	, mLexicon(inLexicon)
	, mIndexNr(inIndexNr)
	, mLastDoc(0)
	, mDocCount(0)
	, mDbDocCount(0)
	, mFlushThread(boost::bind(&M6BasicIx::FlushThread, this))
{
}

M6BasicIx::~M6BasicIx()
{
	if (mFlushThread.joinable())	// left over?
	{
		mFlushQueue.Put(nullptr);
		mFlushThread.join();
	}
}

void M6BasicIx::AddWord(uint32 inWord)
{
	mFullTextIndex.AddWord(mIndexNr, inWord);
}

void M6BasicIx::SetDbDocCount(uint32 inDbDocCount)
{
	mDbDocCount = inDbDocCount;
	mLastDoc = 0;
	mLastTerm = 0;
}

void M6BasicIx::AddDocTerm(uint32 inDoc, uint32 inTerm, uint8 inFrequency, M6OBitStream& inIDL)
{
	if (inTerm != mLastTerm and not Empty())
		FlushTerm(mLastTerm, mDbDocCount);
	
	if (inDoc != 0)
	{
		mLastTerm = inTerm;
		AddDocTerm(inDoc, inFrequency, inIDL);
	}
	else
	{
		mFlushQueue.Put(nullptr);
		mFlushThread.join();
	}
}

void M6BasicIx::AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL)
{
	uint32 d;

	if (mBits.Empty())
	{
		mDocCount = 0;
		d = inDoc;
	}
	else
	{
		assert(inDoc > mLastDoc);
		d = inDoc - mLastDoc;
	}
	
	WriteGamma(mBits, d);
	
	mLastDoc = inDoc;
	++mDocCount;
}

void M6BasicIx::FlushTerm(uint32 inTerm, uint32 inDocCount)
{
	if (mDocCount > 0 and not mBits.Empty())
	{
		mBits.Sync();
		
		M6IBitStream bits(mBits);

		uint32 docNr = 0;

		FlushedTerm* termData = new FlushedTerm;
		termData->mTerm = mLexicon.GetString(inTerm);

		for (uint32 d = 0; d < mDocCount; ++d)
		{
			uint32 delta;
			ReadGamma(bits, delta);
			docNr += delta;
			termData->mDocs.push_back(docNr);
		}

		mFlushQueue.Put(termData);
	}

	mBits.Clear();
	
	mDocCount = 0;
	mLastDoc = 0;
}

void M6BasicIx::FlushThread()
{
	for (;;)
	{
		FlushedTerm* term = mFlushQueue.Get();
		if (term == nullptr)
			break;
		FlushTerm(term);
	}
}

// --------------------------------------------------------------------
// M6StringIx, stores strings in an index, but not in the full text index

class M6StringIx : public M6BasicIx
{
  public:
					M6StringIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
						const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex);
	
	virtual void	AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL);
	virtual void	FlushTerm(uint32 inTerm, uint32 inDocCount);
	
  private:

	virtual void	FlushTerm(FlushedTerm* inTermData);

	vector<uint32>	mDocs;
	
	M6MultiBasicIndex*
					mIndex;
};

M6StringIx::M6StringIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
		const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex)
	: M6BasicIx(inFullTextIndex, inLexicon, inName, inIndexNr)
	, mIndex(dynamic_cast<M6MultiBasicIndex*>(inIndex.get()))
{
	assert(mIndex);
	mFullTextIndex.SetExcludeInFullText(mIndexNr);
}
	
void M6StringIx::AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL)
{
	mDocs.push_back(inDoc);
	mLastDoc = inDoc;
}

void M6StringIx::FlushTerm(uint32 inTerm, uint32 inDocCount)
{
	if (mDocs.size() > 0)
	{
		FlushedTerm* termData = new FlushedTerm;
		termData->mTerm = mLexicon.GetString(inTerm);
		termData->mDocs.swap(mDocs);
		mFlushQueue.Put(termData);
	}
	
	mDocs.clear();
}

void M6StringIx::FlushTerm(FlushedTerm* inTermData)
{
	mIndex->Insert(inTermData->mTerm, inTermData->mDocs);
}

// --------------------------------------------------------------------
//	Text Index contains a full text index

class M6TextIx : public M6BasicIx
{
  public:
	struct FlushedIDLTerm : public FlushedTerm
	{
		int64		mIDLOffset;
	};

					M6TextIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
						const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex);
	virtual			~M6TextIx();

  private:
	void			AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL);
	virtual void	FlushTerm(uint32 inTerm, uint32 inDocCount);

	virtual void	FlushTerm(FlushedTerm* inTermData);

	M6File*			mIDLFile;
	M6OBitStream*	mIDLBits;
	int64			mIDLOffset;
	M6MultiIDLBasicIndex*
					mIndex;
};

M6TextIx::M6TextIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
		const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex)
	: M6BasicIx(inFullTextIndex, inLexicon, inName, inIndexNr)
	, mIDLFile(nullptr)
	, mIDLBits(nullptr)
	, mIDLOffset(0)
	, mIndex(dynamic_cast<M6MultiIDLBasicIndex*>(inIndex.get()))
{
	assert(mIndex);
	mFullTextIndex.SetUsesInDocLocation(mIndexNr);
	mIDLFile = new M6File(
		mFullTextIndex.GetScratchDir().parent_path() / (inName + ".idl"), eReadWrite);
}

M6TextIx::~M6TextIx()
{
	delete mIDLBits;
	delete mIDLFile;
}

void M6TextIx::AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL)
{
	M6BasicIx::AddDocTerm(inDoc, inFrequency, inIDL);
	
	if (mIDLBits == nullptr)
	{
		mIDLOffset = mIDLFile->Seek(0, SEEK_END);
		mIDLBits = new M6OBitStream(*mIDLFile);
	}

	// read binary is much faster than read gamma...
	int64 idlBitSize = inIDL.BitSize();
	if (idlBitSize <= 63)
		WriteBinary(*mIDLBits, 6, idlBitSize);
	else
	{
		WriteBinary(*mIDLBits, 6, 0);
		WriteGamma(*mIDLBits, idlBitSize);
	}

	CopyBits(*mIDLBits, inIDL);
}

void M6TextIx::FlushTerm(uint32 inTerm, uint32 inDocCount)
{
	if (mDocCount > 0 and not mBits.Empty())
	{
		// flush the raw index bits
		mBits.Sync();
		
		if (mIDLBits != nullptr)
			mIDLBits->Sync();

		FlushedIDLTerm* termData = new FlushedIDLTerm;
		termData->mTerm = mLexicon.GetString(inTerm);
		termData->mIDLOffset = mIDLOffset;
		
		M6IBitStream bits(mBits);

		uint32 docNr = 0;
		
		for (uint32 d = 0; d < mDocCount; ++d)
		{
			uint32 delta;
			ReadGamma(bits, delta);
			docNr += delta;
			termData->mDocs.push_back(docNr);
		}

		mFlushQueue.Put(termData);
	}

	mBits.Clear();
	
	delete mIDLBits;
	mIDLBits = nullptr;
	
	mDocCount = 0;
	mLastDoc = 0;
}

void M6TextIx::FlushTerm(FlushedTerm* inTermData)
{
	FlushedIDLTerm* idlTerm = static_cast<FlushedIDLTerm*>(inTermData);
	
	mIndex->Insert(idlTerm->mTerm, idlTerm->mIDLOffset, idlTerm->mDocs);
	delete idlTerm;
}

// --------------------------------------------------------------------
//	Weighted word index, used for ranked searching

class M6WeightedWordIx : public M6BasicIx
{
  public:
	struct FlushedWeightedTerm : public FlushedTerm
	{
		vector<pair<uint32,uint8>>	mWeightedDocs;
	};

					M6WeightedWordIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
						const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex);

	virtual void	AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL);
	virtual void	FlushTerm(uint32 inTerm, uint32 inDocCount);

  private:

	virtual void	FlushTerm(FlushedTerm* inTermData);
	
	M6WeightedBasicIndex*
					mIndex;
};

M6WeightedWordIx::M6WeightedWordIx(M6FullTextIx& inFullTextIndex, M6Lexicon& inLexicon,
		const string& inName, uint8 inIndexNr, M6BasicIndexPtr inIndex)
	: M6BasicIx(inFullTextIndex, inLexicon, inName, inIndexNr)
	, mIndex(dynamic_cast<M6WeightedBasicIndex*>(inIndex.get()))
{
	assert(mIndex);
}

void M6WeightedWordIx::AddDocTerm(uint32 inDoc, uint8 inFrequency, M6OBitStream& inIDL)
{
	uint32 d;

	if (mBits.Empty())
	{
		mDocCount = 0;
		d = inDoc;
	}
	else
	{
		assert(inDoc > mLastDoc);
		d = inDoc - mLastDoc;
	}
	
	WriteGamma(mBits, d);
	
	if (inFrequency < 1)
		inFrequency = 1;
	else if (inFrequency >= kM6MaxWeight)
		inFrequency = kM6MaxWeight;
	
	WriteBinary(mBits, kM6WeightBitCount, inFrequency);
	
	mLastDoc = inDoc;
	++mDocCount;
}

void M6WeightedWordIx::FlushTerm(uint32 inTerm, uint32 inDocCount)
{
	if (mDocCount > 0 and not mBits.Empty())
	{
		// flush the raw index bits
		mBits.Sync();
		
//		vector<pair<uint32,uint8>> docs;
		
		M6IBitStream bits(mBits);
		
		FlushedWeightedTerm* termData = new FlushedWeightedTerm;
		termData->mTerm = mLexicon.GetString(inTerm);

		uint32 docNr = 0;
		for (uint32 d = 0; d < mDocCount; ++d)
		{
			uint32 delta;
			ReadGamma(bits, delta);
			docNr += delta;
			assert(docNr <= inDocCount);
			uint8 weight;
			ReadBinary(bits, kM6WeightBitCount, weight);
			assert(weight > 0);
			termData->mWeightedDocs.push_back(make_pair(docNr, weight));
		}

		mFlushQueue.Put(termData);
	}

	mBits.Clear();
	
	mDocCount = 0;
	mLastDoc = 0;
}

void M6WeightedWordIx::FlushTerm(FlushedTerm* inTermData)
{
	FlushedWeightedTerm* termData = static_cast<FlushedWeightedTerm*>(inTermData);
	mIndex->Insert(termData->mTerm, termData->mWeightedDocs);
	delete termData;
}

// --------------------------------------------------------------------

class M6BatchIndexProcessor
{
  public:
				M6BatchIndexProcessor(M6DatabankImpl& inDatabank, M6Lexicon& inLexicon);
				~M6BatchIndexProcessor();

	void		IndexTokens(const string& inIndexName, M6DataType inDataType,
					const M6InputDocument::M6TokenList& inTokens);
	void		IndexValue(const string& inIndexName, M6DataType inDataType,
					const string& inValue, bool inUnique, uint32 inDocNr);
	void		FlushDoc(uint32 inDocNr);
	void		Finish(uint32 inDocCount);

  private:

	template<class T>
	M6BasicIx*	GetIndexBase(const string& inName, M6IndexType inType, bool inUnique);

	M6FullTextIx		mFullTextIndex;
	M6DatabankImpl&		mDatabank;
	M6Lexicon&			mLexicon;
	
	struct M6BasicIxDesc
	{
		M6BasicIx*		mBasicIx;
		string			mName;
		M6IndexType		mType;
		bool			mUnique;
	};
	typedef vector<M6BasicIxDesc>	M6BasicIxDescList;
	
	M6BasicIxDescList	mIndices;
};

M6BatchIndexProcessor::M6BatchIndexProcessor(M6DatabankImpl& inDatabank, M6Lexicon& inLexicon)
	: mFullTextIndex(inDatabank.GetScratchDir())
	, mDatabank(inDatabank)
	, mLexicon(inLexicon)
{
}

M6BatchIndexProcessor::~M6BatchIndexProcessor()
{
	foreach (M6BasicIxDesc& ix, mIndices)
		delete ix.mBasicIx;
}

template<class T>
M6BasicIx* M6BatchIndexProcessor::GetIndexBase(const string& inName, M6IndexType inType, bool inUnique)
{
	T* result = nullptr;
	foreach (M6BasicIxDesc& ix, mIndices)
	{
		if (inName == ix.mName)
		{
			if (inType != ix.mType or inUnique != ix.mUnique)
				THROW(("Inconsistent use of indices (%s)", inName.c_str()));
			result = dynamic_cast<T*>(ix.mBasicIx);
			break;
		}
	}
	
	if (result == nullptr)
	{
		M6BasicIndexPtr index = mDatabank.CreateIndex(inName, inType, inUnique);

		index->SetAutoCommit(false);
		
		result = new T(mFullTextIndex, mLexicon, inName,
			static_cast<uint8>(mIndices.size() + 1), index);
		
		M6BasicIxDesc desc = { result, inName, inType, inUnique };
		mIndices.push_back(desc);
	}
	
	return result;
}

void M6BatchIndexProcessor::IndexTokens(const string& inIndexName,
	M6DataType inDataType, const M6InputDocument::M6TokenList& inTokens)
{
	if (not inTokens.empty())
	{
		if (inDataType == eM6StringData)
		{
			foreach (uint32 t, inTokens)
				mFullTextIndex.AddWord(0, t);
		}
		else
		{
			M6BasicIx* index = GetIndexBase<M6TextIx>(inIndexName, eM6FullTextIndexType, false);
			foreach (uint32 t, inTokens)
				index->AddWord(t);
		}
	}
}

void M6BatchIndexProcessor::IndexValue(const string& inIndexName,
	M6DataType inDataType, const string& inValue, bool inUnique, uint32 inDocNr)
{
	if (inUnique)
	{
		M6BasicIndexPtr index;
	
		switch (inDataType)
		{
			case eM6StringData:	index = mDatabank.CreateIndex(inIndexName, eM6StringIndexType, true); break;
			case eM6NumberData:	index = mDatabank.CreateIndex(inIndexName, eM6NumberIndexType, true); break;
			case eM6DateData:	index = mDatabank.CreateIndex(inIndexName, eM6DateIndexType, true); break;
			default:			THROW(("Runtime error, unexpected index type"));
		}
		
		index->SetAutoCommit(false);
	
		index->Insert(inValue, inDocNr);
	}
	else if (inValue.length() < kM6MaxKeyLength)
	{
		// too bad, we still have to go through the old route
		M6BasicIx* index;

		switch (inDataType)
		{
			case eM6StringData:	index = GetIndexBase<M6StringIx>(inIndexName, eM6StringIndexType, false); break;
			case eM6NumberData:	index = GetIndexBase<M6StringIx>(inIndexName, eM6NumberIndexType, false); break;
			case eM6DateData:	index = GetIndexBase<M6StringIx>(inIndexName, eM6DateIndexType, false); break;
			default:			THROW(("Runtime error, unexpected index type"));
		}

		mLexicon.LockUnique();
		uint32 t = mLexicon.Store(inValue);
		mLexicon.UnlockUnique();
		
		index->AddWord(t);
	}
}

void M6BatchIndexProcessor::FlushDoc(uint32 inDocNr)
{
	mFullTextIndex.FlushDoc(inDocNr);
}

void M6BatchIndexProcessor::Finish(uint32 inDocCount)
{
	// add the required 'alltext' index
	M6BasicIxDesc allDesc = { new M6WeightedWordIx(mFullTextIndex, mLexicon, "all-text",
		static_cast<uint8>(mIndices.size() + 1), mDatabank.GetAllTextIndex()), "all-text", eM6FullTextIndexType };
	mIndices.push_back(allDesc);
	
	// tell indices about the doc count
	for_each(mIndices.begin(), mIndices.end(), [&inDocCount](M6BasicIxDesc& ix) { ix.mBasicIx->SetDbDocCount(inDocCount); });
	
	// Flush the entry buffer and set up for reading back in the sorted entries
	int64 entryCount = mFullTextIndex.Finish(), entriesRead = 0;
	
	M6Progress progress(entryCount, "assembling index");
	
	// the next loop is very *hot*, make sure it is optimized as much as possible.
	// 
	M6FullTextIx::M6BufferEntry ie = {};
	if (not mFullTextIndex.NextEntry(ie))
		THROW(("Nothing was indexed..."));

	uint32 lastTerm = ie.term;
	uint32 lastDoc = ie.doc;
	uint32 termFrequency = ie.weight;

	uint32 exclude = mFullTextIndex.GetFullTextIxMap();

	do
	{
		assert(ie.term > lastTerm or ie.term == lastTerm and ie.doc >= lastDoc);

		++entriesRead;
	
		if ((entriesRead % 10000) == 0)
			progress.Progress(entriesRead);

		if (lastDoc != ie.doc or lastTerm != ie.term)
		{
			if (termFrequency > 0)
				mIndices.back().mBasicIx->AddDocTerm(lastDoc, lastTerm, termFrequency, ie.idl);

			lastDoc = ie.doc;
			lastTerm = ie.term;
			termFrequency = 0;
		}
		
		if (ie.ix > 0)
			mIndices[ie.ix - 1].mBasicIx->AddDocTerm(ie.doc, ie.term, ie.weight, ie.idl);
		
		if ((exclude & (1 << ie.ix)) == 0)
			termFrequency += ie.weight;
		
		if (termFrequency > numeric_limits<uint8>::max())
			termFrequency = numeric_limits<uint8>::max();
	}
	while (mFullTextIndex.NextEntry(ie));

	// flush
	for_each(mIndices.begin(), mIndices.end(), [&ie](M6BasicIxDesc& ix) { ix.mBasicIx->AddDocTerm(0, 0, 0, ie.idl); });
	
	progress.Progress(entriesRead);
}

// --------------------------------------------------------------------

M6DatabankImpl::M6DatabankImpl(M6Databank& inDatabank, const fs::path& inPath, MOpenMode inMode)
	: mDatabank(inDatabank)
	, mDbDirectory(inPath)
	, mMode(inMode)
	, mStore(nullptr)
	, mBatch(nullptr)
{
	if (not fs::exists(mDbDirectory) and inMode == eReadWrite)
	{
		fs::create_directory(mDbDirectory);
		fs::create_directory(mDbDirectory / "tmp");
		
		mStore = new M6DocStore(mDbDirectory / "data", eReadWrite);
		mAllTextIndex.reset(new M6SimpleWeightedIndex(mDbDirectory / "all-text.index", eReadWrite));
	}
	else if (not fs::is_directory(mDbDirectory))
		THROW(("databank path is invalid (%s)", inPath.string().c_str()));
	else
	{
		mStore = new M6DocStore(mDbDirectory / "data", inMode);
		mAllTextIndex.reset(new M6SimpleWeightedIndex(mDbDirectory / "all-text.index", inMode));
		
		if (fs::exists(mDbDirectory / "all-text.weights"))
		{
			try
			{
				uint32 maxDocNr = mStore->NextDocumentNumber();
				mDocWeights.assign(maxDocNr, 0);
				
				M6File file(mDbDirectory / "all-text.weights", eReadOnly);
				if (file.Size() == sizeof(float) * maxDocNr)
					file.Read(&mDocWeights[0], sizeof(float) * maxDocNr);
				else
					mDocWeights.clear();
			}
			catch (...)
			{
				mDocWeights.clear();
			}
		}
		
		if (mDocWeights.empty())
			RecalculateDocumentWeights();
	}
}

M6DatabankImpl::~M6DatabankImpl()
{
	mStore->Commit();
	delete mStore;
}

M6BasicIndexPtr M6DatabankImpl::GetIndex(const string& inName, M6IndexType inType, bool inUnique)
{
	M6BasicIndexPtr result;
	
	foreach (M6IndexDesc& desc, mIndices)
	{
		if (desc.mName == inName)
		{
			if (desc.mType != inType or desc.mUnique != inUnique)
				THROW(("Inconsistent use of indices (%s)", inName.c_str()));
			
			result = desc.mIndex;
			break;
		}
	}
	
	return result;
}

M6BasicIndexPtr M6DatabankImpl::CreateIndex(const string& inName, M6IndexType inType, bool inUnique)
{
	M6BasicIndexPtr result = GetIndex(inName, inType, inUnique);
	if (result == nullptr)
	{
		fs::path path = mDbDirectory / (inName + ".index");
		
		if (inUnique)
		{
			switch (inType)
			{
				case eM6StringIndexType:
				case eM6DateIndexType:		result.reset(new M6SimpleIndex(path, mMode)); break;
				case eM6NumberIndexType:	result.reset(new M6NumberIndex(path, mMode)); break;
				default:					THROW(("unsupported"));
			}
		}
		else
		{
			switch (inType)
			{
				case eM6DateIndexType:		result.reset(new M6SimpleMultiIndex(path, mMode)); break;
				case eM6NumberIndexType:	result.reset(new M6NumberMultiIndex(path, mMode)); break;
				case eM6StringIndexType:	result.reset(new M6SimpleMultiIndex(path, mMode)); break;
				case eM6FullTextIndexType:	result.reset(new M6SimpleIDLMultiIndex(path, mMode)); break;
				default:					THROW(("unsupported"));
			}
		}

		mIndices.push_back(M6IndexDesc(inName, result, inType, inUnique));
	}
	return result;
}

void M6DatabankImpl::StoreThread()
{
	for (;;)
	{
		M6InputDocument* doc = mStoreQueue.Get();
		if (doc == nullptr)
			break;
		
		doc->Store();
		mIndexQueue.Put(doc);
	}
	
	mIndexQueue.Put(nullptr);
}

void M6DatabankImpl::IndexThread()
{
	for (;;)
	{
		M6InputDocument* doc = mIndexQueue.Get();
		if (doc == nullptr)
			break;
		
		uint32 docNr = doc->GetDocNr();
		assert(docNr > 0);
		
		foreach (const M6InputDocument::M6IndexTokens& d, doc->GetIndexTokens())
			mBatch->IndexTokens(d.mIndexName, d.mDataType, d.mTokens);

		foreach (const M6InputDocument::M6IndexValue& v, doc->GetIndexValues())
			mBatch->IndexValue(v.mIndexName, v.mDataType, v.mIndexValue, v.mUnique, docNr);

		mBatch->FlushDoc(docNr);
		
		delete doc;
	}
}

void M6DatabankImpl::Store(M6Document* inDocument)
{
	M6InputDocument* doc = dynamic_cast<M6InputDocument*>(inDocument);
	if (doc == nullptr)
		THROW(("Invalid document"));

//	uint32 docNr = doc->Store();
//	
	if (mBatch != nullptr)
		mStoreQueue.Put(doc);
//	{
//		foreach (const M6InputDocument::M6IndexTokens& d, doc->GetIndexTokens())
//			mBatch->IndexTokens(d.mIndexName, d.mDataType, d.mTokens);
//
//		foreach (const M6InputDocument::M6IndexValue& v, doc->GetIndexValues())
//			mBatch->IndexValue(v.mIndexName, v.mDataType, v.mIndexValue, v.mUnique, docNr);
//
//		mBatch->FlushDoc(docNr);
//	}
//	
//	delete inDocument;
}

M6Document* M6DatabankImpl::Fetch(uint32 inDocNr)
{
	M6Document* result = nullptr;
	
	uint32 docPage, docSize;
	if (mStore->FetchDocument(inDocNr, docPage, docSize))
		result = new M6OutputDocument(mDatabank, inDocNr, docPage, docSize);

	return result;
}

M6Document* M6DatabankImpl::FindDocument(const string& inIndex, const string& inValue)
{
	//M6BasicIndexPtr index = CreateIndex(inIndex, eM6StringIndexType);
	//if (not index)
	//	THROW(("Index %s not found", inIndex.c_str()));
	//
	//uint32 v;
	//if (not index->Find(ba::to_lower_copy(inValue), v))
	//	THROW(("Value %s not found in index %s", inValue.c_str(), inIndex.c_str()));
	//
	//return Fetch(v);
	return nullptr;
}

class M6Accumulator
{
  public:
				M6Accumulator(uint32 inDocCount)
					: mItems(reinterpret_cast<M6Item*>(calloc(sizeof(M6Item), inDocCount)))
					, mFirst(nullptr), mDocCount(inDocCount), mHitCount(0) {}
				
				~M6Accumulator()
				{
					free(mItems);
				}
				
	float		Add(uint32 inDocNr, float inDelta)
				{
					if (mItems[inDocNr].mCount++ == 0)
					{
						mItems[inDocNr].mNext = mFirst;
						mFirst = &mItems[inDocNr];
						++mHitCount;
					}
					
					return mItems[inDocNr].mValue += inDelta;
				}

	float		operator[](uint32 inIndex) const	{ return mItems[inIndex].mValue; }
	
	void		Collect(vector<uint32>& outDocs, uint32 inTermCount)
				{
					outDocs.reserve(mHitCount);
					for (M6Item* item = mFirst; item != nullptr; item = item->mNext)
					{
						if (item->mCount >= inTermCount)
							outDocs.push_back(static_cast<uint32>(item - mItems));
					}
				}

  private:
	
	struct M6Item
	{
		float	mValue;
		uint32	mCount;
		M6Item*	mNext;
	};
	
	M6Item*		mItems;
	M6Item*		mFirst;
	uint32		mDocCount, mHitCount;
};

M6Iterator* M6DatabankImpl::Find(const string& inQuery, uint32 inReportLimit)
{
	if (mDocWeights.empty())
		RecalculateDocumentWeights();

	M6Iterator* result = nullptr;

//	uint32 docCount = mStore->size();
	uint32 maxDocNr = mStore->NextDocumentNumber();
	float maxD = static_cast<float>(maxDocNr);

	float queryWeight = 0, Smax = 0;
	M6Accumulator A(maxDocNr);
	
	// for each term
	{
		M6WeightedBasicIndex::M6WeightedIterator iter;
		if (static_cast<M6WeightedBasicIndex*>(mAllTextIndex.get())->Find(inQuery, iter))
		{
			uint8 termWeight = 1;	// ?
		
			const float c_add = 0.007f;
			const float c_ins = 0.12f;
	
			float s_add = c_add * Smax;
			float s_ins = c_ins * Smax;
			
			float idf = log(1.f + maxD / iter.Size());
			float wq = idf * termWeight;
			queryWeight += wq * wq;
			
			uint8 f_add = static_cast<uint8>(s_add / (idf * wq * wq));
			uint8 f_ins = static_cast<uint8>(s_ins / (idf * wq * wq));
			
			uint32 docNr;
			uint8 weight;
			while (iter.Next(docNr, weight) and weight >= f_add)
			{
				if (weight >= f_ins or A[docNr] != 0)
				{
					float wd = weight;
					float sd = idf * wd * wq;
					
					float S = A.Add(docNr, sd);
					if (Smax < S)
						Smax = S;
				}
			}
		}
	}
		
	queryWeight = sqrt(queryWeight);
	
	vector<uint32> docs;
	A.Collect(docs, 1);
	
	auto compare = [](const pair<uint32,float>& a, const pair<uint32,float>& b) -> bool
						{ return a.second > b.second; };
	vector<pair<uint32,float>> best;
	
	foreach (uint32 doc, docs)
	{
		float docWeight = mDocWeights[doc];
		float rank = A[doc] / (docWeight * queryWeight);
		
		if (best.size() < inReportLimit)
		{
			best.push_back(make_pair(doc, rank));
			push_heap(best.begin(), best.end(), compare);
		}
		else if (best.front().second < rank)
		{
			pop_heap(best.begin(), best.end(), compare);
			best.back() = make_pair(doc, rank);
			push_heap(best.begin(), best.end(), compare);
		}
	}
	
	sort_heap(best.begin(), best.end(), compare);
	
	foreach (auto b, best)
	{
		auto_ptr<M6Document> doc(Fetch(b.first));

		cout << doc->GetAttribute("id") << '\t'
			<< b.second << '\t'
			<< doc->GetAttribute("title") << endl;
	}
	
	return result;
}

void M6DatabankImpl::StartBatchImport(M6Lexicon& inLexicon)
{
	mBatch = new M6BatchIndexProcessor(*this, inLexicon);
	
	mStoreThread = boost::thread(boost::bind(&M6DatabankImpl::StoreThread, this));
	mIndexThread = boost::thread(boost::bind(&M6DatabankImpl::IndexThread, this));
}

void M6DatabankImpl::CommitBatchImport()
{
	mStoreQueue.Put(nullptr);
	mStoreThread.join();
	mIndexThread.join();
	
	mBatch->Finish(mStore->size());
	delete mBatch;
	mBatch = nullptr;

	foreach (M6IndexDesc& desc, mIndices)
		desc.mIndex->SetAutoCommit(true);
	
	// And clean up
	fs::remove_all(mDbDirectory / "tmp");

	Vacuum();
	RecalculateDocumentWeights();
}

void M6DatabankImpl::RecalculateDocumentWeights()
{
	uint32 docCount = mStore->size();
	uint32 maxDocNr = mStore->NextDocumentNumber();
	
	// recalculate document weights
	
	mDocWeights.assign(maxDocNr, 0);
	M6WeightedBasicIndex* ix = dynamic_cast<M6WeightedBasicIndex*>(mAllTextIndex.get());
	if (ix == nullptr)
		THROW(("Invalid index"));
	
	M6Progress progress(ix->size(), "calculating weights");
	ix->CalculateDocumentWeights(docCount, mDocWeights, progress);

	M6File weightFile(mDbDirectory / "all-text.weights", eReadWrite);
	weightFile.Write(&mDocWeights[0], sizeof(float) * mDocWeights.size());
}

void M6DatabankImpl::Vacuum()
{
	int64 size = mAllTextIndex->size();
	
	foreach (M6IndexDesc& desc, mIndices)
		size += desc.mIndex->size();
	
	M6Progress progress(size + 1, "vacuuming");

	mAllTextIndex->Vacuum(progress);
	foreach (M6IndexDesc& desc, mIndices)
		desc.mIndex->Vacuum(progress);
	progress.Consumed(1);
}

void M6DatabankImpl::Validate()
{
	mStore->Validate();
	mAllTextIndex->Validate();
}

void M6DatabankImpl::DumpIndex(const string& inIndex, ostream& inStream)
{
	if (inIndex != "all-text")
		THROW(("Dumping of other indices not supported yet"));
	
	foreach (const string& key, *mAllTextIndex)
		inStream << key << endl;
}

// --------------------------------------------------------------------

M6Databank::M6Databank(const fs::path& inPath, MOpenMode inMode)
	: mImpl(new M6DatabankImpl(*this, inPath, inMode))
{
}

M6Databank::~M6Databank()
{
	delete mImpl;
}

M6Databank* M6Databank::CreateNew(const fs::path& inPath)
{
	if (fs::exists(inPath))
		fs::remove_all(inPath);

	return new M6Databank(inPath, eReadWrite);
}

void M6Databank::StartBatchImport(M6Lexicon& inLexicon)
{
	mImpl->StartBatchImport(inLexicon);
}

void M6Databank::CommitBatchImport()
{
	mImpl->CommitBatchImport();
}

void M6Databank::Store(M6Document* inDocument)
{
	mImpl->Store(inDocument);
}

M6DocStore& M6Databank::GetDocStore()
{
	return mImpl->GetDocStore();
}

M6Document* M6Databank::Fetch(uint32 inDocNr)
{
	return mImpl->Fetch(inDocNr);
}

M6Document* M6Databank::FindDocument(const string& inIndex, const string& inValue)
{
	return mImpl->FindDocument(inIndex, inValue);
}

M6Iterator* M6Databank::Find(const string& inQuery, uint32 inReportLimit)
{
	return mImpl->Find(inQuery, inReportLimit);
}

uint32 M6Databank::size() const
{
	return mImpl->GetDocStore().size();
}

void M6Databank::RecalculateDocumentWeights()
{
	mImpl->RecalculateDocumentWeights();
}

void M6Databank::Vacuum()
{
	mImpl->Vacuum();
}

void M6Databank::Validate()
{
	mImpl->Validate();
}

void M6Databank::DumpIndex(const string& inIndex, ostream& inStream)
{
	mImpl->DumpIndex(inIndex, inStream);
}
