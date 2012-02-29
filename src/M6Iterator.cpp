#include "M6Lib.h"

#include <boost/foreach.hpp>
#define foreach BOOST_FOREACH

#include "M6Iterator.h"

using namespace std;

void M6Iterator::Intersect(vector<uint32>& ioDocs, M6Iterator* inIterator)
{
	// merge boolean filter result and ranked results
	vector<uint32> docs;
	swap(docs, ioDocs);
	ioDocs.reserve(docs.size());
	
	uint32 db;
	float r;
	bool empty = not inIterator->Next(db, r);
	vector<uint32>::iterator dr = docs.begin();
	
	while (not empty and dr != docs.end())
	{
		if (*dr == db)
		{
			ioDocs.push_back(db);
			++dr;
			empty = not inIterator->Next(db, r);
		}
		else if (*dr < db)
			++dr;
		else
			inIterator->Next(db, r);
	}
}

// --------------------------------------------------------------------

M6UnionIterator::M6UnionIterator()
{
}

M6UnionIterator::M6UnionIterator(M6Iterator* inA, M6Iterator* inB)
{
	AddIterator(inA);
	AddIterator(inB);
}

M6UnionIterator::~M6UnionIterator()
{
	foreach (M6IteratorPart& part, mIterators)
		delete part.mIter;
	mIterators.clear();
}

void M6UnionIterator::AddIterator(M6Iterator* inIter)
{
	M6IteratorPart p = { inIter };
	
	float r;
	if (inIter->Next(p.mDoc, r))
	{
		mIterators.push_back(p);
		push_heap(mIterators.begin(), mIterators.end(), greater<M6IteratorPart>());
	}
	else
		delete inIter;
}

bool M6UnionIterator::Next(uint32& outDoc, float& outRank)
{
	bool result = false;

	if (not mIterators.empty())
	{
		pop_heap(mIterators.begin(), mIterators.end(), greater<M6IteratorPart>());
		
		outDoc = mIterators.back().mDoc;
		outRank = 1.0f;
		result = true;
		
		for (;;)
		{
			uint32 d;
			float r;

			if (mIterators.back().mIter->Next(d, r))
			{
				mIterators.back().mDoc = d;
				push_heap(mIterators.begin(), mIterators.end(), greater<M6IteratorPart>());
				
				if (mIterators.front().mDoc > outDoc)
					break;
			}
			else
			{
				delete mIterators.back().mIter;
				mIterators.pop_back();

				if (mIterators.empty())
					break;
			}
			
			pop_heap(mIterators.begin(), mIterators.end(), greater<M6IteratorPart>());
		}
	}
	
	return result;
}

M6Iterator* M6UnionIterator::Create(M6Iterator* inA, M6Iterator* inB)
{
	M6Iterator* result;
	if (inA == nullptr)
		result = inB;
	else if (inB == nullptr)
		result = inA;
	else
		result = new M6UnionIterator(inA, inB);
	return result;
}

// --------------------------------------------------------------------

M6IntersectionIterator::M6IntersectionIterator()
{
}

M6IntersectionIterator::M6IntersectionIterator(M6Iterator* inA, M6Iterator* inB)
{
	AddIterator(inA);
	AddIterator(inB);
}

M6IntersectionIterator::~M6IntersectionIterator()
{
	foreach (M6IteratorPart& part, mIterators)
		delete part.mIter;
	mIterators.clear();
}

void M6IntersectionIterator::AddIterator(M6Iterator* inIter)
{
	M6IteratorPart p = { inIter };
	
	float r;
	if (inIter->Next(p.mDoc, r))
		mIterators.push_back(p);
	else
		delete inIter;
}

bool M6IntersectionIterator::Next(uint32& outDoc, float& outRank)
{
	bool result = false, done = mIterators.empty();
	float r;
	
	while (not (result or done))
	{
		sort(mIterators.begin(), mIterators.end());

		outDoc = mIterators.back().mDoc;
		if (mIterators.front().mDoc == outDoc)
		{
			result = true;
			foreach (M6IteratorPart& part, mIterators)
				done = done or part.mIter->Next(part.mDoc, r) == false;
			break;
		}
		
		foreach (M6IteratorPart& part, mIterators)
		{
			while (part.mDoc < outDoc)
				done = done or part.mIter->Next(part.mDoc, r) == false;
		}
	}

	if (done)
	{
		foreach (M6IteratorPart& part, mIterators)
			delete part.mIter;
		mIterators.clear();
	}

	return result;
}

M6Iterator* M6IntersectionIterator::Create(M6Iterator* inA, M6Iterator* inB)
{
	M6Iterator* result;
	if (inA == nullptr)
		result = inB;
	else if (inB == nullptr)
		result = inA;
	else
		result = new M6IntersectionIterator(inA, inB);
	return result;
}
