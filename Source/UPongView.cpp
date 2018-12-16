// copyright (c) 1990-2017 by Digidesign, Inc. All rights reserved. 

/*****************************************************************************

	UPongView.cpp
	
	Easter-egg asteroids game. To play, enter one of the magic words into the 
	Track Comments field, then open a surround panner on that track. 
	
	The game is designed to have a minimal footprint - once the intial TPongView
	object is created, there are no additional heap allocations - all new
	Objects are created on the stack from a pre-allocated memory pool (using
	placement new to get the benefits of the usual constructor logic.)
	
	For stereo panners, a single TPongView will be created and both sides of
	the panner will point to it with a shared_ptr. Each side of the panner
	calls Draw() with its own painter, and the right-side will be adjusted to
	handle the horiz range [200, 400].
	
	Ted Barram 4/29/17
*****************************************************************************/

#include "UPongView.h"
#include <list>
#include <map>

/*---------------------------------------------------------------------------*/

const bool kNoObjects = true; // set true if you just want to fly around with no distractions
const bool kNoGravity = false; // turn off gravity
const bool kIgnoreLevels = true; // this short-circuits the level-changing code (UpdateLevel)
const bool kGravityObjects = true;
const bool kNoFlatEarth = true;
const double kGroundSpeed = 150;

// minimap settings
const int32_t kMinimapHeight = 30;
const int32_t kMinimapOuterRatio = 4;
const std::pair<int32_t, int32_t> kMiniMapTopLeftCorner = {80, 60};

/*---------------------------------------------------------------------------*/


using CRect = Rectangle<int32_t>; // x,y,w,h
using CRectF = Rectangle<float>;
using CPoint = Point<float>;
using Cmn_Point32 = Point<int32_t>;
using DFW_ImageRef = void*;

int Cmn_Max(int a, int b) { return a > b ? a : b; }
double Cmn_Max(double a, double b) { return a > b ? a : b; }
int Cmn_Min(int a, int b) { return a < b ? a : b; }

void CMN_DEBUGASSERT(bool cond)
{
	if (!cond)
	{
		int bp = 0;
		bp++;
	}
}

void CMN_ASSERT(bool cond) { CMN_DEBUGASSERT(cond); }
void CMN_ASSERT(void* ptr) { CMN_DEBUGASSERT(ptr != nullptr); }

// utils
namespace pong
{

const std::string cImagesFolderDir = "/Users/tbarram/workspaces/JUCE/SpaceForce/Images/IconImages";
const std::string cFlatEarthImagePath = "/Users/tbarram/workspaces/JUCE/SpaceForce/Images/vibe_meter_glow_48.png";
const std::string cChaserImagePath = "/Users/tbarram/workspaces/JUCE/SpaceForce/Images/IconImages/bomb.png";
const std::string cBackgroundImagePath = "/Users/tbarram/workspaces/JUCE/SpaceForce/Images/stars.png";
const std::string cImagesPath = "/Users/tbarram/workspaces/JUCE/SpaceForce/Images/IconImages/";
const Colour cShipColor = juce::Colours::black;
const Colour cShipEdgeColor = juce::Colours::aliceblue;
const Colour cShipEdgeBrightColor = juce::Colours::yellow;
const Colour cGroundColor = juce::Colours::red;
const Colour cTextColor = juce::Colours::lawngreen;
const double PI = 3.1415926;
	
const int32_t kGridHeight = 800;
const int32_t kGridWidth = 1200;
	
// tweak these for performance and sensitivity of controls
const int32_t kRefreshRateMS = 30;
const int32_t kAnimateThrottleMS = 10;
const double kRotateSpeed = PI/20;
const double kThrustSpeed = 20;
const bool kFreezeShipInMiddle = false;

/*---------------------------------------------------------------------------*/
int32_t rnd(int32_t max) { return ::rand() % max; }
int32_t rnd(int32_t min, int32_t max) { return min + ::rand() % (max - min); }
double rndf() { return ((double)rnd(1000) / 1000.0); }
double rndf(int32_t min, int32_t max) { return ((double)rnd(min, max)); }
	
/*---------------------------------------------------------------------------*/
int32_t Interpolate(float a1, float a2, float a, float b1, float b2)
{
	return b1 + (((a - a1) * (b2 - b1)) / (a2 - a1));
}

/*---------------------------------------------------------------------------*/
void Bound(double& val, const double low, const double hi)
{
	if (val < low)
		val = low;
	else if (val > hi)
		val = hi;
}

// update once every wakeup
int64_t gNowMS = 0;
int64_t gStartTimeMS = 0;

// CVector
// do the math in doubles to handle rounding
struct CVector
{
	CVector() : mX(0), mY(0) {}
	CVector(double x, double y) : mX(x), mY(y) {}
	double mX;
	double mY;
	
	CVector& operator+=(const CVector& rhs)
	{
		mX += rhs.mX;
		mY += rhs.mY;
		return *this;
	}
	
	CVector& operator*(const double& rhs)
	{
		mX *= rhs;
		mY *= rhs;
		return *this;
	}
	
	// takes a raw angle, or the pre-computed sin & cos
	static CVector Velocity(const double speed, const double angle, std::pair<double, double> trig = {0.0f,0.0f})
	{
		const double sin_ = trig.first != 0 ? trig.first : ::sin(angle);
		const double cos_ = trig.second != 0 ? trig.second : ::cos(angle);
		return CVector(speed * sin_, -(speed * cos_));
	}
};
	
/*---------------------------------------------------------------------------*/
double DistanceSq(const CVector& p1, const CVector& p2)
{
	return std::pow((p1.mX - p2.mX), 2) + std::pow((p1.mY - p2.mY), 2);
}
	
/*---------------------------------------------------------------------------*/
double Distance(const CVector& p1, const CVector& p2)
{
	return sqrt(DistanceSq(p1, p2));
}

/*---------------------------------------------------------------------------*/
struct CState
{
	CState() {}
	CState(CVector pos, CVector vel, CVector acc, int64_t lifetime, int32_t killedBy) :
		mPos(pos), mVel(vel), mAcc(acc),
		mExpireTimeMS(lifetime ? gNowMS + lifetime : 0),
		mKilledBy(killedBy)
	{}
	
	void Log()
	{
		printf("CState mPos: (%f,%f), mVel: (%f,%f), mAcc: (%f,%f) \n",
			  mPos.mX, mPos.mY, mVel.mX, mVel.mY, mAcc.mX, mAcc.mY );
	}
		
	CVector mPos;	// position
	CVector mVel;	// velocity
	CVector mAcc;	// acceleration
	
	// if !0 then this is the time at which the object expires
	int64_t	mExpireTimeMS;
	
	// bitmask of which Object types can destroy this object
	int32_t mKilledBy;
};

/*---------------------------------------------------------------------------*/
// EObjectType
// can be OR'd together in a bitmask
enum EObjectType
{
	eNull = 0,
	eShip			= 1 << 0,
	eBullet			= 1 << 1,
	eFragment		= 1 << 2,
	eShipFragment	= 1 << 3,
	eIcon			= 1 << 4,
	eVector			= 1 << 5,
	eChaser			= 1 << 6,
	eGround			= 1 << 7,
	eFlatEarth		= 1 << 8,
	eGravity		= 1 << 9,
	eMiniMap		= 1 << 10,
	eAll			= 0xFFFF
};

/*---------------------------------------------------------------------------*/
enum ECollisionType
{
	eNormal			= 0,
	eWithGround		= 1,
	eSmart			= 2
};


// a fixed-size array of pos/time pairs
const int32_t kMaxNumVectorPoints = 32;
typedef std::pair<CVector, int64_t> VectorPoint;
typedef VectorPoint VectorPath[kMaxNumVectorPoints];
struct VectorPathElement
{
	CVector mPos;
	int64_t mMoveTime;
	int64_t mPauseTime;
};

const int32_t kMutantPathSize = 12;
VectorPathElement MutantPath[kMutantPathSize] = {	{{200,40},0,2000}, {{200,100},200,1000}, {{450,60},100,500}, {{350,60},100,400},
													{{500,60},100,400}, {{200,60},100,300}, {{500,60},100,300}, {{300,60},100,300},
													{{110,60},100,300}, {{90,60},100,500}, {{160,60},100,400}, {{80,120},100,1000}};

	
const CVector kMiniMapTopLeftCornerV = CVector(kMiniMapTopLeftCorner.first, kMiniMapTopLeftCorner.second);
const int32_t kMinimapWidth = kGridWidth * kMinimapHeight / kGridHeight;
const int32_t kMinimapOuterHeight = kMinimapOuterRatio * kMinimapHeight;
const int32_t kMinimapOuterWidth = kMinimapOuterRatio * kMinimapWidth;
const CVector kMiniMapCenter = CVector(kMiniMapTopLeftCornerV.mX + (kMinimapWidth/2),
										kMiniMapTopLeftCornerV.mY + (kMinimapHeight/2));
const CVector kMiniMapOuterTopLeftCorner = CVector(kMiniMapCenter.mX - (kMinimapOuterWidth/2),
												   kMiniMapCenter.mY - (kMinimapOuterHeight/2));
	
// TranslateForMinimap
CVector TranslateForMinimap(const CVector& p)
{
	const double x = Interpolate(0, kGridWidth, p.mX, kMiniMapTopLeftCornerV.mX, kMiniMapTopLeftCornerV.mX + kMinimapWidth);
	const double y = Interpolate(kGridHeight, 0, p.mY, kMiniMapTopLeftCornerV.mY + kMinimapHeight, kMiniMapTopLeftCornerV.mY);
	return CVector(x, y);
}

} // pong namespace

using namespace pong;
class TPongView;

// CObject
class CObject
{
public:

	CObject() :
		mType(eNull),
		mInUse(false)
	{}
	
	CObject(TPongView* pongView, const EObjectType type, const CState state) :
		mPongView(pongView),
		mType(type),
		mState(state),
		mHitPoints(1),
		mReadyAfterMS(0),
		mNumAnimates(0),
		mReady(true),
		mHasFriction(true),
		mIsFixed(false),
		mBoundVelocity(true),
		mColor(0),
		mMass(0),
		mImage(nullptr),
		mImageName(""),
		mWidth(0),
		mHeight(0),
		mNext(nullptr),
		mInUse(true),
		mParent(nullptr),
		mAngle(0.0),
		mAngleSin(0.0),
		mAngleCos(0.0),
		mThrusting(false),
		mDockedToEarthMS(0),
		mVectorIndex(0),
		mNumVectorPoints(0),
		mLastVectorPointMS(0),
		mHasTriggeredNext(false)
	{
		this->Init();
	}
	
	// this only gets called when the pool goes away - it does not get called
	// when an object gets released back into the pool
	virtual ~CObject() {}
	
	void		Animate(const double diffSec);
	void		AnimateShip();
	void		AnimateChaser();
	void		AnimateMiniMapObject();
	void		Draw(Graphics& g);
	void		DrawShip(Graphics& g);
	void		DrawGroundObject(Graphics& g);
	
	std::string		GetName() const { return mName; }
	EObjectType		Type() const { return mType; }
	
	void			CalcPosition(const double diffSec);
	void			VectorCalc(const double diffSec);
	static bool		CollidedWith(CRect& a, CRect& b);
	bool			CollidedWith(CObject& other);
	static bool		CollidedWithGround(CObject& ground, CObject& obj);
	static bool		IsUnderLine(CVector right, CVector left, CVector pt);
	static void		LineBetween(Graphics& g, CVector p1, CVector p2, int32_t size = 2);
	void			SetNumHitPoints(int32_t hits) { mHitPoints = hits; }
	int32_t			GetNumHitPoints() const { return mHitPoints; }
	void			SetDockedToEarth() { mDockedToEarthMS = gNowMS + 1000; }
	bool			IsDockedToEarth() const { return mDockedToEarthMS != 0; }
	void			Collided(ECollisionType type);
	int32_t			GetKilledBy() const { return mState.mKilledBy; }
	bool			IsKilledBy(EObjectType type) { return mState.mKilledBy & type; }
	void			ShipDestroyed();
	bool			IsAlive() const;
	void			Died();
	bool			IsReady() const { return mReady && (mReadyAfterMS == 0 || gNowMS > mReadyAfterMS); }
	void			SetReadyAfter(int64_t ms) { mReady = true; mReadyAfterMS = ms; }
	void			SetReady(bool ready) { mReady = ready; }
	bool			IsDestroyed() const { return !this->Is(eShip) && mHitPoints <= 0; }
	bool			HasGravity() const { return this->GetMass() != 0; }
	bool			Is(EObjectType type) const { return mType == type; }
	bool			IsOneOf(int32_t types) const { return types & mType; }
	bool			WrapsHorizontally() const { return /*this->Is(eShip) ||*/ this->Is(eFlatEarth); }
	CVector			Pos() const { return mState.mPos; }
	CVector			Vel() const { return mState.mVel; }
	CVector			Acc() const { return mState.mAcc; }
	int32 			GetMass() const { return mMass; }
	void			IncrementAcc(const CVector& acc) { mState.mAcc.mX += acc.mX; mState.mAcc.mY += acc.mY;}
	void			SetAcc(const CVector& acc) { mState.mAcc.mX = acc.mX; mState.mAcc.mY = acc.mY;}
	void			SetMass(double mass) { mMass = mass; mHasFriction = false; mBoundVelocity = false; }
	void			SetFixed() { mIsFixed = true; }
	bool			IsFixed() { return mIsFixed; }
	void			SetName(std::string name) { mName = name; }
	void			SetColor(Colour color) { mColor = color; }
	void			SetWidthAndHeight(int32_t w, int32_t h) { mWidth = w; mHeight = h; }
	void			SetImage(Image* img);
	void			SetParent(CObject* parent){ mParent = parent; mParent->SetChild(this); }
	void			SetChild(CObject* child) { mChild = child; }
	CObject*		GetChild() const { return mChild; }
	CObject*		GetParent() const { return mParent; }
	bool 			IsOffscreen() const { return (mState.mPos.mY > kGridHeight || mState.mPos.mY < 0 ||
												  mState.mPos.mX > kGridWidth || mState.mPos.mX < 0); }
	
	// a point 25 pixels above the center
	CVector	FlatEarthDockPoint() const { return CVector(mState.mPos.mX, mState.mPos.mY - 25); }
	
	// for ship object
	void			Rotate(CPoint& p, const CPoint& c);
	void			GetControlData();
	double			GetAngle() const { return mAngle; }
	double			GetSin() const { return mAngleSin; }
	double			GetCos() const { return mAngleCos; }
	CVector			GetFront() const { return {(double)mFront.x, (double)mFront.y}; }
	std::vector<Cmn_Point32> GetVertices() { return mVertices; }
	
	// stop gravity if the ship is on the ground
	bool 			IsOnGround() const { return (mState.mPos.mY >= (kGridHeight - 50)); }
	
	// for vector objects
	VectorPath&	GetVectorPath() { return mVectorPath; }
	void				AddVectorPathElement(VectorPathElement vpe);
	int32_t				GetNumVectorPoints() const { return mNumVectorPoints; }
	
	// for use with CObjectPool
	void		SetNext(CObject* next) { mNext = next; }
	CObject*	GetNext() { return mNext; }
	bool		InUse() const { return mInUse; }
	
	
	/*---------------------------------------------------------------------------*/
	static double ArcTan2(const CObject& obj1, const CObject& obj2)
	{
		return ::atan2(obj1.Pos().mX - obj2.Pos().mX, obj1.Pos().mY - obj2.Pos().mY);
	}
	
	/*---------------------------------------------------------------------------*/
	static int32_t DegreesFromRadians(const double radians)
	{
		return (radians * 180) / PI;
	}
	
	// used in pool and ready
	bool IsActive() const { return this->InUse() && this->IsReady(); }
	
	// release the object's resources and mark the object slot as available - we need to
	// do this explicitly since the destructor doesn't get called since the objects are
	// coming from a pool
	void Free()
	{
		mInUse = false;
	}
	
protected:
	TPongView*					mPongView;
	const EObjectType			mType;
	CState						mState;
	int32_t						mHitPoints;
	int64_t						mReadyAfterMS;
	CRect						mRect;
	std::vector<Cmn_Point32>	mVertices;
	int32_t						mNumAnimates;
	bool						mReady;
	bool						mHasFriction;
	bool						mIsFixed;
	bool						mBoundVelocity;
	Colour						mColor;
	double						mMass;
	std::string					mName;
	
	// only used by bullets to support CCD (Continuous Collision Detection)
	#define						kNumPreviousPositions 8
	CRect						mPreviousRect[kNumPreviousPositions];
	
private:
	void Init();
	
	// image data
	Image* mImage;
	std::string mImageName;
	
	// object size
	int32_t mWidth;
	int32_t mHeight;
	
	// for use with CObjectPool
	CObject* mNext;
	bool mInUse;
	
	CObject* mParent;
	CObject* mChild;
	
	// for ship object
	double		mAngle;
	double		mAngleSin;
	double		mAngleCos;
	CPoint		mFront;
	std::vector<Cmn_Point32> mThrustVertices;
	bool		mThrusting;
	int64_t		mDockedToEarthMS;
	
	// for vector objects
	int32_t				mVectorIndex;
	int32_t				mNumVectorPoints;
	int64_t				mLastVectorPointMS;
	VectorPath	mVectorPath;
	
	// for ground objects
	CVector	mLeftPoint;
	CVector	mRightPoint;
	bool			mHasTriggeredNext;
};


// CObjectPool
class CObjectPool
{
public:
	static const int32_t kMaxNumObjects = 1024;
	
	// Init
	void Init()
	{
		mFirstOpenSlot = &mPool[0];
		
		// init mNext on all objects
		for (int k = 0; k < kMaxNumObjects; k++)
			mPool[k].SetNext((k == kMaxNumObjects - 1) ? nullptr : &mPool[k + 1]);
	}
	
	// NewObject
	CObject* NewObject(TPongView* pongView, const EObjectType type, const CState state)
	{
		// if we hit this assert then we've exceeded kMaxNumObjects
		CMN_DEBUGASSERT(mFirstOpenSlot != nullptr);
		if (mFirstOpenSlot == nullptr)
			return nullptr;
		
		// find the next open memory slot
		CObject* newObject = mFirstOpenSlot;
		mFirstOpenSlot = newObject->GetNext();

		// use placement new to construct the object at the open memory slot in the pool
		// (no heap allocations)
		return new (newObject) CObject(pongView, type, state);
	}
	
	// AddGroundObject
	// we keep a separate list of GroundObject pointers so we don't have to iterate as
	// much through the objects for collisions, and since ground collisions are special
	void AddGroundObject(CObject* obj)
	{
		mGroundObjectList.push_back(obj);
	}
	
	// Animate - animates all the objects
	void Animate(double diffSec)
	{
		mNumActiveObjects = 0;
		mNumOffscreenGravityObjects = 0;
		// go through the pool looking for ready objects
		for (int k = 0; k < kMaxNumObjects; k++)
		{
			CObject& obj = mPool[k];
			
			if (!obj.IsActive())
				continue;
			
			mNumActiveObjects++;
			obj.Animate(diffSec);
			
			if (obj.IsOffscreen() && obj.Is(eGravity))
				mNumOffscreenGravityObjects++;
			
			// if the object is now dead, remove it from the pool
			if (!obj.IsAlive())
			{
				obj.Died();
				obj.Free();
				
				if (obj.Is(eGround))
					mGroundObjectList.remove(&obj);
				
				// release this slot back into the pool
				obj.SetNext(mFirstOpenSlot);
				mFirstOpenSlot = &obj;
			}
		}
	}
	
	// Draw
	// draw all the objects
	void Draw(Graphics& g)
	{
		for (int k = 0; k < kMaxNumObjects; k++)
		{
			CObject& obj = mPool[k];
			
			if (!obj.IsActive())
				continue;
			
			obj.Draw(g);
		}
	}
	
	// CheckCollision
	static void CheckCollision(CObject& obj1, CObject& obj2)
	{
		if ( !obj1.IsKilledBy(obj2.Type()) && !obj2.IsKilledBy(obj1.Type()) )
			return;
			
		if (obj1.CollidedWith(obj2))
		{
			// collisions are symmetric
			// the new bullet collision logic invalidates this (right?)
			CMN_DEBUGASSERT(obj2.CollidedWith(obj1));
			
			if (obj1.IsKilledBy(obj2.Type()))
				obj1.Collided(eNormal);
			
			if (obj2.IsKilledBy(obj1.Type()))
				obj2.Collided(eNormal);
		}
	}
	
	// HandleObjectPairInteractions
	// call CheckCollision exactly once on all active object pairs
	void HandleObjectPairInteractions()
	{
		for (int32_t k = 0; k < kMaxNumObjects - 1; k++)
		{
			CObject& obj1 = mPool[k];
			
			if (!obj1.IsActive() || obj1.Is(eGround))
				continue;
			
			for (int32_t j = k + 1; j < kMaxNumObjects; j++)
			{
				CObject& obj2 = mPool[j];
				
				if (!obj2.IsActive() || obj2.Is(eGround))
					continue;
				
				this->CheckCollision(obj1, obj2);
				
				if (obj1.HasGravity() && obj2.HasGravity())
					this->ApplyGravity(obj1, obj2);
			}
		}
	}
	
	void ResetGravityAcc()
	{
		// TODO: store gravity objects in their own list to optimize?
		for (int32_t k = 0; k < kMaxNumObjects; k++)
		{
			CObject& obj = mPool[k];
			if (obj.HasGravity())
				obj.SetAcc({0,0});
		}
	}
	
	void DestroyAllGravityObjects()
	{
		// TODO: store gravity objects in their own list to optimize?
		for (int32_t k = 0; k < kMaxNumObjects; k++)
		{
			CObject& obj = mPool[k];
			if (obj.HasGravity() && !obj.Is(eShip))
				obj.SetNumHitPoints(0);
		}
	}
	
	// CheckCollisionsWithGround
	void CheckCollisionsWithGround()
	{
		for (int32_t k = 0; k < kMaxNumObjects; k++)
		{
			CObject& obj = mPool[k];
			
			if (!obj.IsActive() || obj.Is(eGround) ||
				!obj.IsKilledBy(eGround) || obj.IsDockedToEarth())
				continue;
			
			for (auto g : mGroundObjectList)
			{
				if (CObject::CollidedWithGround(*g, obj))
					obj.Collided(eWithGround);
			}
		}
	}
	
	// KillAllObjectsOfType
	void KillAllObjectsOfType(int32_t types)
	{
		for (int32_t k = 0; k < kMaxNumObjects; k++)
		{
			CObject& obj = mPool[k];
			if (obj.IsActive() && obj.IsOneOf(types))
				obj.Collided(eSmart);
		}
	}
	
	int32_t GetNumActiveObjects() const { return mNumActiveObjects; }
	int32_t GetNumOffscreenGravityObjects() const { return mNumOffscreenGravityObjects; }
	void ApplyGravity(CObject& obj1, CObject& obj2);
	
private:
	CObject mPool[kMaxNumObjects];
	CObject* mFirstOpenSlot;
	std::list<CObject*> mGroundObjectList;
	int32_t mNumActiveObjects;
	int32_t mNumOffscreenGravityObjects;
};

// TPongView
class TPongView : public IPongView
{
public:
	TPongView() :
		mShipObject(nullptr),
		mFlatEarthObject(nullptr),
		mLastDrawMS(0),
		mNextNewFallingIconObjectMS(0),
		mNextNewCrawlingIconObjectMS(0),
		mNextNewVectorIconObjectMS(0),
		mNextNewChaserObjectMS(0),
		mVectorObjectActive(false),
		mShowGuideEndMS(0),
		mOneTimeGuideExplosion(true),
		mShowLevelTextUntilMS(0),
		mLevel(0),
		mNextLevelKills(0),
		mKills(0),
		mDeaths(0),
		mNumSmartBombs(4),
		mIconCount(0),
		mVectorCount(0),
		mAutoSmartBombMode(false),
		mIsPaused(false),
		mNumGravityObjects(0)
	{}
	~TPongView() {}
	
	// public interface
	virtual void Draw(Graphics& g) override;
	virtual int32_t GetRefreshrateMS() override { return kRefreshRateMS; }
	virtual int32_t GetGridWidth() override { return kGridWidth; };
	virtual int32_t GetGridHeight() override { return kGridHeight; };
	virtual void SetSongName(std::string name) override { mSongName = name; }
	virtual void InstallMusicCallback(std::function<void()> f) override { mMusicCallback = f; }
	void Animate();
	void CreateNewObjects();
	void UpdateLevel();
	void CheckDockedToEarth();
	void DoExplosions();
	bool CheckKeyPress(char key, int32_t throttleMS);
	void VectorObjectDied();
	void ChaserObjectDied();
	bool LevelPause() const { return mShowLevelTextUntilMS != 0; }
	int32_t GetGridWidth() const { return kGridWidth; }
	int32_t GetGridHeight() const { return kGridHeight; }
	CObjectPool& GetObjectPool() { return mObjectPool; }
	void AddKill() { mKills++; }
	void AddDeath() { mDeaths++; }
	void Explosion(const CVector& pos, bool isShip = false);
	int32_t GetIconCount() const { return mIconCount; }
	CObject* GetFlatEarthObject() const { return mFlatEarthObject; }
	CObject* GetShipObject() const { return mShipObject; }
	void NewGroundObject(CVector pos);
	CObject* NewObject(const EObjectType type, const CState state, bool minimap = false);
	std::vector<Image>& GetImages() { return mImages; }
	Image& GetFlatEarthImage() { return mFlatEarthImage; }
	Image& GetChaserImage() { return mChaserImage; }
	CVector& GetChaserPosition();
	void AddChaserPosition(CVector& vec);
	void CreateGravityObjects();
		
private:
	
	void			Init();
	void			Free();
	void			DrawText(Graphics& g);
	void			NewFallingIconObject();
	void			NewCrawlingIconObject();
	void			NewChaserObject();
	CObject*		NewGravityObject(CVector pos, double mass, std::string name);
	void			NewVectorIconObject();
	void			ShootBullet();
	void			SmartBomb();
	
private:
	
	CObject*		mShipObject;
	CObject*		mFlatEarthObject;
	CObject*		mChaserObject;
	int64_t			mLastDrawMS;
	int64_t			mNextNewFallingIconObjectMS;
	int64_t			mNextNewCrawlingIconObjectMS;
	int64_t			mNextNewVectorIconObjectMS;
	int64_t			mNextNewChaserObjectMS;
	bool			mVectorObjectActive;
	int64_t			mShowGuideEndMS;
	bool			mOneTimeGuideExplosion;
	int64_t			mShowLevelTextUntilMS;
	int32_t			mLevel;
	int32_t			mNextLevelKills;
	int32_t			mKills;
	int32_t			mDeaths;
	int32_t			mNumSmartBombs;
	int32_t			mIconCount;
	int32_t			mVectorCount;
	bool			mAutoSmartBombMode;
	bool			mIsPaused;
	
	std::string				mSongName;
	std::function<void()> 	mMusicCallback;
	
	static const int32_t sChaserPositionMax = 512;
	int32_t	mChaserPositionIndex;
	CVector mChaserPositions[sChaserPositionMax];
	
	std::vector<Image> 	mImages;
	Image 				mGravityImages[16];
	Image				mBlackHoleMinimapImage;
	Image				mBlackHoleImage;
	int32_t    			mNumGravityObjects;
	Image 				mFlatEarthImage;
	Image				mChaserImage;
	Image				mBackgroundImage;
	int32_t				mGravityIndex;
	std::map<char, int64_t> mLastKeyPressTimeMS;
	
	CObjectPool		mObjectPool;
	
	friend IPongView;
	typedef std::shared_ptr<TPongView> PongViewPtr;
};


/*---------------------------------------------------------------------------*/
// 	METHOD:	Create - factory
//  tbarram 3/13/17
/*---------------------------------------------------------------------------*/
IPongViewPtr IPongView::Create()
{
	TPongView::PongViewPtr pongView = std::make_shared<TPongView>();
	pongView->Init();
	return pongView;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	Init
//  tbarram 4/29/17
/*---------------------------------------------------------------------------*/
void TPongView::Init()
{
	gStartTimeMS = Time::getCurrentTime().toMilliseconds();
	gNowMS = gStartTimeMS;
	mLastDrawMS = gNowMS;
	mShowGuideEndMS = gNowMS + 1000; // show the guide for 4 seconds
	
	mObjectPool.Init();
	
	// populate the mImages vector
	{
		DirectoryIterator dir_iter(File(cImagesFolderDir), false);
		while(dir_iter.next())
		{
			File file(dir_iter.getFile());
			if (file.getFileExtension() == ".png")
			{
				const Image& img = ImageFileFormat::loadFrom(file);
				if (img.isValid())
					mImages.push_back(img);
			}
		}
	}
	
	// create the flat earth object
	if (!kNoFlatEarth)
	{
		mFlatEarthImage = ImageFileFormat::loadFrom(File(cFlatEarthImagePath));
		CMN_ASSERT(mFlatEarthImage.isValid());
		
		const CVector p(float(this->GetGridWidth()/2), float(this->GetGridHeight() - 350));
		const CVector v(-20, 0); // flat earth moves to the left
		const CVector a(0, 0);
		mFlatEarthObject = this->NewObject(eFlatEarth, {p, v, a, 0, 0});
		CMN_ASSERT(mFlatEarthObject);
		mFlatEarthObject->SetReady(true);
	}
	
	{
		mChaserImage = ImageFileFormat::loadFrom(File(cChaserImagePath));
		CMN_ASSERT(mChaserImage.isValid());
		//mNextNewChaserObjectMS = gNowMS + 5000;
	}
	
	{
		mBackgroundImage = ImageFileFormat::loadFrom(File(cBackgroundImagePath));
		CMN_ASSERT(mBackgroundImage.isValid());
	}
	
	// create ship object
	{
		const CVector p(float(this->GetGridWidth()/2), float(this->GetGridHeight()/2) - 20);
		const CVector v(0, 0);
		const CVector a(20, kNoGravity ? 0 : 80);
		mShipObject = this->NewObject(eShip, {p, v, a, 0, eIcon|eVector|eGround}, true);
		CMN_ASSERT(mShipObject);
		
		mShipObject->SetNumHitPoints(6);
		mShipObject->SetName("Ship");
		mShipObject->GetChild()->SetColor(Colours::red);
	}
	
	if (kGravityObjects)
	{
		this->CreateGravityObjects();
		
		// this makes the ship part of the gravity group
		mShipObject->SetMass(20.0); // fun at 20, 100, 200, ...
	}
}

/*---------------------------------------------------------------------------*/
void TPongView::CreateGravityObjects()
{
	mNumGravityObjects = 0;
	//NewGravityObject({rndf(400,500), 200}, rndf(9,10), "bomb"); 	// 4.0 	// 4.0  // 10.0
	//NewGravityObject({rndf(650,700), 300}, rndf(4,6), "galaxy"); 	// 1.0 	// 7.0	// 7.0
	//NewGravityObject({rndf(470,530), 350}, rndf(1.8,2.5), "apple"); 	// 10.0 // 10.0	// 4.0
	NewGravityObject({rndf(570,630), 390}, rndf(1.8,2.2), "chrome");
	NewGravityObject({rndf(670,730), 190}, rndf(1.8,2.2), "Gtr_Tuner_E_Flat");
	NewGravityObject({rndf(600,650), 290}, rndf(180,200), "astronaut30");
	
	// note: for the mass of the black hole, I suspect that we're hitting the
	// kMaxG bound in ApplyGravity so these giant masses don't change anything
	//const CVector p(rnd(1700, 2400), rnd(-750, -850));
	const CVector p(kGridWidth - 100, 60);
	CObject* blackHole = NewGravityObject(p, rndf(10000,20000), "apple");
	blackHole->SetFixed();
	
	const std::string deathStar64 = cImagesPath + "DeathStar64.png";
	mBlackHoleImage = ImageFileFormat::loadFrom(File(deathStar64));
	CMN_ASSERT(mBlackHoleImage.isValid());
	blackHole->SetImage(&mBlackHoleImage);
	
	//CMN_ASSERT(blackHole->GetChild());
	//blackHole->GetChild()->SetColor(Colours::black);
	//blackHole->GetChild()->SetWidthAndHeight(8, 8);
	
	///const std::string deathStar = cImagesPath + "DeathStar.png";
	//mBlackHoleMinimapImage = ImageFileFormat::loadFrom(File(deathStar));
	//CMN_ASSERT(mBlackHoleMinimapImage.isValid());
	//blackHole->GetChild()->SetImage(&mBlackHoleMinimapImage);
	//blackHole->GetChild()->SetWidthAndHeight(16, 16);
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	Draw
//  tbarram 5/5/17
/*---------------------------------------------------------------------------*/
void TPongView::Draw(Graphics& g)
{
	// update the global now
	gNowMS = Time::getCurrentTime().toMilliseconds();
	
	if (mBackgroundImage.isValid())
	{
		//g.setTiledImageFill(mBackgroundImage, 0, 0, 1.0f);
		//g.fillAll();
		//g.drawImageAt(mBackgroundImage, 0, 0);
	}
	
	// draw minimap outline
	g.drawRect(kMiniMapTopLeftCornerV.mX, kMiniMapTopLeftCornerV.mY, kMinimapWidth, kMinimapHeight, 1);
	g.drawRect(kMiniMapOuterTopLeftCorner.mX, kMiniMapOuterTopLeftCorner.mY, kMinimapOuterWidth, kMinimapOuterHeight, 1);
	
	// calc the new positions, check keypresses, etc.
	this->Animate();
	
	// draw all the objects
	mObjectPool.Draw(g);
	
	this->DrawText(g);
	
	// M key advances to the next song
	if (this->CheckKeyPress('m', 1000) && mMusicCallback)
		mMusicCallback();
	
	// P key toggles paused state
	if (this->CheckKeyPress('p', 1000))
		mIsPaused = !mIsPaused;
	
	if (this->CheckKeyPress('g', 1000))
	{
		static bool enabled = true;
		mObjectPool.DestroyAllGravityObjects();
		
		if (enabled)
			this->CreateGravityObjects();
		
		enabled = !enabled;
	}
}

/*---------------------------------------------------------------------------*/
// DrawText
/*---------------------------------------------------------------------------*/
void TPongView::DrawText(Graphics& g)
{
	// show the guide for 4 seconds at start and then blow it up
	// TODO: move this into UpdateLevel()
	if (mShowGuideEndMS > gNowMS)
	{
		CRect rect(0, 0, 600, 400);
		const std::string text = "L / R arrows rotate\nX shoots\nZ thrusts\nS smart bomb";
		g.setColour(Colours::lawngreen);
		g.drawText(text, rect, Justification::centred, true);
	}
	else if (mOneTimeGuideExplosion)
	{
		mOneTimeGuideExplosion = false;
		this->DoExplosions();
	}
	else
	{
		const int32_t elapsedSec = (int32_t)(gNowMS - gStartTimeMS) / 1000;
		// draw text box in bottom right corner  GetNumActiveObjects
		const CRect rect(20, this->GetGridHeight() - 20, this->GetGridWidth(), 200); // x,y,w,h
		const std::string text =
				"\nlevel: " + std::to_string(mLevel) +
				"\t\tkills: " + std::to_string(mKills) +
				"\t\tdeaths: " + std::to_string(mDeaths) +
				"\t\thp: " +	std::to_string(mShipObject->GetNumHitPoints()) +
				"\t\tbombs: " + std::to_string(mNumSmartBombs) +
				"\t\ttime: " + std::to_string(elapsedSec) + " sec" +
				"\t\tobjects: " + std::to_string(mObjectPool.GetNumActiveObjects()) +
				"\t\tsong: " + mSongName;
		
		g.setColour(Colours::honeydew);
		g.drawFittedText(text, rect, Justification::left, true);
	}
	
	// show level text
	if (this->LevelPause())
	{
		CRect rect(0, 0, this->GetGridWidth(), 400); // x,y,w,h
		const std::string text = "LEVEL " + std::to_string(mLevel);
		g.setColour(Colours::lawngreen);
		g.drawText(text, rect, Justification::centred, true);
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	DoExplosions
//   - do a bunch of explosions in the middle of the (left) screen
//  tbarram 5/14/17
/*---------------------------------------------------------------------------*/
void TPongView::DoExplosions()
{
	// tweak these 2 settings to adjust explosions
	const int32_t kNumExplosions = 70;
	const int32_t rangeH = 700;
	
	const int32_t mid = this->GetGridWidth() / 2;
	const int32_t startingH = mid - (rangeH/2);
	
	for (int32_t k = 0; k < kNumExplosions; k++)
	{
		const CVector e = {double(startingH + rnd(rangeH)), double(rnd(40, 100))};
		this->Explosion(e);
	}
}

/*---------------------------------------------------------------------------*/
bool TPongView::CheckKeyPress(char key, int32_t throttleMS)
{
	if (KeyPress::isKeyCurrentlyDown(key) &&
		((gNowMS - mLastKeyPressTimeMS[key]) > throttleMS))
	{
		mLastKeyPressTimeMS[key] = gNowMS;
		return true;
	}
	else
		return false;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	Animate
//   - main wakeup
//   - this is what drives all the object updates, state changes, etc.
//   - will come randomly, but will be fast since we call repaint a lot
//  tbarram 12/3/16
/*---------------------------------------------------------------------------*/
void TPongView::Animate()
{
	// get the time diff since last wakeup
	const double diffMS = gNowMS - mLastDrawMS;
	if (diffMS < kAnimateThrottleMS)
		return;
	
	const double diffSec = diffMS / 1000.0;
	mLastDrawMS = gNowMS;
	
	if (mIsPaused)
		return;
	
	// see if it's time for the next level
	this->UpdateLevel();
	
	// see if it's time to create new objects
	if (!this->LevelPause())
		this->CreateNewObjects();
	
	// animate all the objects
	mObjectPool.Animate(diffSec);
	
	mObjectPool.ResetGravityAcc();
	mObjectPool.HandleObjectPairInteractions();
	mObjectPool.CheckCollisionsWithGround();
	
	// shoot
	if (this->CheckKeyPress('x', 0) || this->CheckKeyPress(KeyPress::spaceKey, 0))
		this->ShootBullet();
	
	// smart bomb
	if (KeyPress::isKeyCurrentlyDown('s') || mAutoSmartBombMode)
		this->SmartBomb();
	
	// see if we should dock
	this->CheckDockedToEarth();
	
	if (this->CheckKeyPress('o', 700))
		mAutoSmartBombMode = !mAutoSmartBombMode;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	CheckDockedToEarth
//  tbarram 6/10/17
/*---------------------------------------------------------------------------*/
void TPongView::CheckDockedToEarth()
{
	if (mFlatEarthObject && !mShipObject->IsDockedToEarth())
	{
		const double d = Distance(mFlatEarthObject->FlatEarthDockPoint(), mShipObject->Pos());
		
		// if we're close to the earth, and we're moving slowly and pointing up, then dock
		if (d < 50 && fabs(mShipObject->GetAngle()) < PI/4 &&
			fabs(mShipObject->Vel().mX) < 32 && fabs(mShipObject->Vel().mY) < 32)
		{
			mShipObject->SetDockedToEarth();
			mNumSmartBombs++;
		}
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	UpdateLevel
//  tbarram 5/14/17
/*---------------------------------------------------------------------------*/
void TPongView::UpdateLevel()
{
	// un-pause the level screen
	if (this->LevelPause() && gNowMS > mShowLevelTextUntilMS)
	{
		mShowLevelTextUntilMS = 0;
		this->DoExplosions();
	}
	
	// L advances level
	if (this->CheckKeyPress('L', 700))
		mKills = mNextLevelKills;
	
	// update level based on kills
	if (mKills >= mNextLevelKills)
	{
		mLevel++;
		mNumSmartBombs++; // extra smart bomb with each new level
		
		// start the level pause screen
		if (mLevel > 1)
		{
			this->SmartBomb();
			mShowLevelTextUntilMS = gNowMS + 3000;
		}
		
		int32_t killsToAdvance = kIgnoreLevels ? 100000 : 4;
		switch (mLevel)
		{
			case 1:
				this->NewGroundObject({(double)this->GetGridWidth(), (double)this->GetGridHeight() - 50});
				
				// level 1 - falling objects only
				if (!kNoObjects)
				{
					mNextNewFallingIconObjectMS = mShowGuideEndMS;
					killsToAdvance = 15;
				}
				break;
			case 2:
				// level 2 - vector objects only
				mNextNewFallingIconObjectMS = 0;
				mNextNewVectorIconObjectMS = gNowMS;
				killsToAdvance = 10; // need fewer kills to advance past this level since vector objects are slow
				break;
			case 3:
				// level 3 - falling & crawling
				mNextNewFallingIconObjectMS = gNowMS;
				mNextNewCrawlingIconObjectMS = gNowMS;
				mNextNewVectorIconObjectMS = 0;
				killsToAdvance = 20;
				break;
			case 4:
			{
				// start the ground
				//this->NewGroundObject({(double)this->GetGridWidth(), (double)this->GetGridHeight() - 20});
				
				// no objects for 10 seconds, then start the earth object, vector + crawling
				if (mFlatEarthObject)
					mFlatEarthObject->SetReadyAfter(gNowMS + 3000);
				
				mNextNewFallingIconObjectMS = 0;
				mNextNewVectorIconObjectMS = gNowMS + 10000;
				mNextNewCrawlingIconObjectMS = gNowMS + 10000;
				killsToAdvance = 50;
				break;
			}
			case 5:
				// level 5 and above - all objects & ground
				mNextNewFallingIconObjectMS = gNowMS;
				killsToAdvance = 80;
				break;
		}
		
		mNextLevelKills += killsToAdvance;
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	CreateNewObjects
//  tbarram 5/14/17
/*---------------------------------------------------------------------------*/
void TPongView::CreateNewObjects()
{
	// create a new falling icon object if it's time, and schedule the next one
	if (mNextNewFallingIconObjectMS && gNowMS > mNextNewFallingIconObjectMS)
	{
		this->NewFallingIconObject();
		
		// schedule the next one - they get faster as more kills accumulate
		const int32_t kMultiplier = (mLevel - 1);
		const int32_t fixedMS = std::max(500 - (kMultiplier * 100), 100);
		const int32_t randomMaxMS = std::max(1200 - (kMultiplier * 100), 500);
		const int32_t nextMS = (fixedMS + (rnd(randomMaxMS)));
		mNextNewFallingIconObjectMS = gNowMS + nextMS;
	}
	
	// create a new crawling icon object if it's time, and schedule the next one
	if (mNextNewCrawlingIconObjectMS && gNowMS > mNextNewCrawlingIconObjectMS)
	{
		this->NewCrawlingIconObject();
		mNextNewCrawlingIconObjectMS = (gNowMS + 3000);
	}
	
	// create a new vector icon object if it's time, and schedule the next one
	if (mNextNewVectorIconObjectMS && !mVectorObjectActive && gNowMS > mNextNewVectorIconObjectMS)
	{
		this->NewVectorIconObject();
	}
	
	if (mNextNewChaserObjectMS && gNowMS > mNextNewChaserObjectMS)
	{
		mNextNewChaserObjectMS = 0;
		this->NewChaserObject();
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	NewObject
//  tbarram 4/30/17
/*---------------------------------------------------------------------------*/
CObject* TPongView::NewObject(const EObjectType type, const CState state, bool minimap)
{
	CObject* obj = mObjectPool.NewObject(this, type, state);
	
	if (minimap)
	{
		CObject* miniMapObj = this->NewObject(eMiniMap, {{0,0}, {0,0}, {0,0}, 0, 0});
		miniMapObj->SetParent(obj);
	}
	
	return obj;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	NewGravityObject
/*---------------------------------------------------------------------------*/
CObject* TPongView::NewGravityObject(CVector pos, double mass, std::string name)
{
	const CVector v(0, 0);
	const CVector a(0, 0);
	const int32_t killedBy = eBullet;
	CObject* obj = this->NewObject(eGravity, {pos, v, a, 0, killedBy}, true);
	obj->SetMass(mass);
	obj->SetName(name);
	
	// yuk - fix this mess - store all the images in the main list
	const std::string filePath = cImagesPath + name + ".png";
	mGravityImages[mNumGravityObjects] = ImageFileFormat::loadFrom(File(filePath));;
	CMN_ASSERT(&mGravityImages[mNumGravityObjects]);
	obj->SetImage(&mGravityImages[mNumGravityObjects]);
	mNumGravityObjects++;
	
	return obj;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	NewGroundObject
//  tbarram 5/29/17
/*---------------------------------------------------------------------------*/
void TPongView::NewGroundObject(CVector pos)
{
	const CVector v(-kGroundSpeed, 0);
	const CVector a(0, 0);
	this->GetObjectPool().AddGroundObject(this->NewObject(eGround, {pos, v, a, 0, 0}));
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	NewFallingIconObject
//  tbarram 4/30/17
/*---------------------------------------------------------------------------*/
void TPongView::NewFallingIconObject()
{
	// create a new object with random horiz start location and gravity -
	// they have 0 velocity, only (random) vertical acceleration
	const int32_t horizMaxStart = this->GetGridWidth() - 10;
	const CVector p(rnd(horizMaxStart), 0);
	const CVector v(0, 0);
	const CVector a(0, 5 + rnd(100));
	const int32_t killedBy = eBullet | eShip | eGround;
	this->NewObject(eIcon, {p, v, a, 0, killedBy});
	mIconCount++;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	NewCrawlingIconObject
//   - these objects crawl horizontally across the top of the screen
//  tbarram 4/30/17
/*---------------------------------------------------------------------------*/
void TPongView::NewCrawlingIconObject()
{
	const CVector p(this->GetGridWidth(), 20 + rnd(20));
	const CVector v(-(30 + rnd(50)), 0);
	const CVector a(-(5 + rnd(50)), 0);
	const int32_t killedBy = eBullet | eShip | eGround;
	this->NewObject(eIcon, {p, v, a, 0, killedBy});
}

/*---------------------------------------------------------------------------*/
void TPongView::NewChaserObject()
{
	const CVector p(this->GetGridWidth()/2, 200);
	const CVector v(0, 0);
	const CVector a(0, 0);
	const int32_t killedBy = eBullet;
	this->NewObject(eChaser, {p, v, a, 0, killedBy});
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	NewVectorIconObject
//   - these objects follow a point-to-point path, pausing at each point then moving
//     linearly to the next point
//  tbarram 4/30/17
/*---------------------------------------------------------------------------*/
void TPongView::NewVectorIconObject()
{
	mVectorObjectActive = true;
			
	const CVector p(0, 0);
	const CVector v(0, 0);
	const CVector a(0, 0);
	const int32_t killedBy = eBullet | eShip | eShipFragment;
	CObject* obj = this->NewObject(eVector, {p, v, a, 0, killedBy});
	if (!obj)
		return;
	
	if (mVectorCount++ % 5 == 0)
	{
		// do the mutant path every 5th time
		for (int k = 0; k < kMutantPathSize; k++)
			obj->AddVectorPathElement(MutantPath[k]);
	}
	else
	{
		// else do a random path
		const int32_t kNumPoints = 4;

		// add the random points in the path
		for (int k = 0; k < kNumPoints; k++)
		{
			const double horiz = 20 + rnd(this->GetGridWidth() - 40);
			const int32_t maxVert = k <= 3 ? 200 : 760; // start in the top half
			const double vert = 10 + rnd(maxVert - 10);
			const int64_t timeMoving = 200 + rnd(2000);
			const int64_t timePausing = 500 + rnd(2000);
			
			obj->AddVectorPathElement({{horiz, vert}, timeMoving, timePausing});
		}
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	ShootBullet
//  tbarram 4/30/17
/*---------------------------------------------------------------------------*/
void TPongView::ShootBullet()
{
	// start the bullet at the front of the ship in the ship's direction
	static const double speed = 1600.0; // 400.0
	const CVector v = CVector::Velocity(speed, 0, {mShipObject->GetSin(), mShipObject->GetCos()});
	const CVector a(0,0); // bullets have no acceleration, just a constant velocity
	const int64_t lifetime = 5000; // 1 second
	this->NewObject(eBullet, {mShipObject->GetFront(), v, a, lifetime, eIcon | eVector});
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	SmartBomb
//  tbarram 5/8/17
/*---------------------------------------------------------------------------*/
void TPongView::SmartBomb()
{
	if (!this->LevelPause())
	{
		//if (mNumSmartBombs > 0)
		//	mNumSmartBombs--;
		//else
		//	return;
	}

	mObjectPool.KillAllObjectsOfType(eIcon | eVector);
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	VectorObjectDied
//  tbarram 5/8/17
/*---------------------------------------------------------------------------*/
void TPongView::VectorObjectDied()
{
	if (mVectorObjectActive)
	{
		mVectorObjectActive = false;
		mNextNewVectorIconObjectMS = gNowMS;
	}
}

/*---------------------------------------------------------------------------*/
void TPongView::ChaserObjectDied()
{
	mNextNewChaserObjectMS = gNowMS + 10000;
}

/*---------------------------------------------------------------------------*/
CVector& TPongView::GetChaserPosition()
{
	// make chaserDistance get bigger and smaller
	const int32_t chaserDistance = 60; // 30
	const int32_t index = (mChaserPositionIndex + (sChaserPositionMax - chaserDistance)) % sChaserPositionMax;
	return mChaserPositions[index];
}

/*---------------------------------------------------------------------------*/
void TPongView::AddChaserPosition(CVector& vec)
{
	mChaserPositions[mChaserPositionIndex] = vec;
	mChaserPositionIndex = (mChaserPositionIndex + 1) % sChaserPositionMax;
}

/*---------------------------------------------------------------------------*/
// ApplyGravity
// this applies the gravity acceleration vectors in both directions
void CObjectPool::ApplyGravity(CObject& obj1, CObject& obj2)
{
	// these settings affect the gravity and heavily impact the gameplay
	static const double kMinG = 20.0;
	static const double kMaxG = 70.0;
	static const double G = 9800;
	
	const double d = Distance(obj1.Pos(), obj2.Pos());
	const double g = (G * obj1.GetMass() * obj2.GetMass()) / (d * d);
	
	// bound the gravity to a specific range
	double g_adjusted = g + kMinG;
	Bound(g_adjusted, kMinG, kMaxG);
	
	const double angleRad = CObject::ArcTan2(obj1, obj2);
	
	// create the acceleration vectors in both directions
	CVector a(g_adjusted * ::sin(angleRad), g_adjusted * ::cos(angleRad));
	CVector a_neg(-a.mX, -a.mY);
	
	// apply them
	obj1.IncrementAcc(a_neg);
	obj2.IncrementAcc(a);
	
	//const double degreesForLog = CObject::DegreesFromRadians(angleRad);
	//printf("angle between %s & %s: %0.2f deg, d: %0.2f, g: %0.2f, g_adjusted: %02f \n",
	//	   obj1.GetName().c_str(), obj2.GetName().c_str(), degreesForLog, d, g, g_adjusted);
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	ShipDestroyed
//  tbarram 5/8/17
/*---------------------------------------------------------------------------*/
void CObject::ShipDestroyed()
{
	mState.mPos = {float(mPongView->GetGridWidth()/2), float(mPongView->GetGridHeight()/2 - 20)};
	mState.mVel = {0, -20}; // start with a little upward thurst since gravity will quickly kick in
	mState.mAcc = {20, 80}; // reset (does this ever change?)
	mAngle = 0.0;
	this->SetReadyAfter(gNowMS + 2000); // hide ship for a few seconds when it gets destroyed
	this->SetNumHitPoints(6); // reset
	mVertices.clear();
	mDockedToEarthMS = 0;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	IsAlive
//   - when an object returns false, it will be removed from the list and deleted
//  tbarram 4/30/17
/*---------------------------------------------------------------------------*/
bool CObject::IsAlive() const
{
	if (this->Is(eShip))
		return true;
	
	if (this->GetParent())
		return this->GetParent()->IsAlive();
	
	if (this->IsDestroyed())
		return false;
	
	if (this->Is(eGround))
		return mRightPoint.mX > 0;
	
	if (mState.mExpireTimeMS && gNowMS > mState.mExpireTimeMS)
		return false;
	
	// objects that leave the bottom edge never come back
	if (!this->Is(eGravity) && mState.mPos.mY >= kGridHeight)
		return false;
	
	// all non-ship objects die when they disappear
	if (!this->Is(eShip) && !this->Is(eChaser) && !this->Is(eGravity) &&
		(mState.mPos.mX < -10 || mState.mPos.mX > (mPongView->GetGridWidth() + 10)))
		return false;
	
	return true;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	Died
/*---------------------------------------------------------------------------*/
void CObject::Died()
{
	if (this->Is(eVector))
		mPongView->VectorObjectDied();
	
	if (this->Is(eChaser))
		mPongView->ChaserObjectDied();
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	Explode
//   - send a bunch of fragments off from the passed-in position
//  tbarram 4/30/17
/*---------------------------------------------------------------------------*/
void TPongView::Explosion(const CVector& pos, bool isShip)
{
	const int32_t kNumFrags = isShip ? 22 : rnd(6, 12);
	const double kAngleInc = 2 * PI / kNumFrags;
	
	for (int32_t j = 0; j < kNumFrags; j++)
	{
		// give each frag a random speed
		const double speed = rnd(30, 80);
		
		// send each of the fragments at an angle equally spaced around the unit
		// circle, with some randomness
		const double rndMin = -PI/(isShip ? 4 : 8);
		const double rndMax = PI/(isShip ? 4 : 8);
		const double angleRnd = rndMin + ((rndMax - rndMin) * rndf());
		const CVector v = CVector::Velocity(speed, (j * kAngleInc) + angleRnd);
		
		// give each frag a random H/V acceleration
		const double accelH = isShip ? 0 : rnd(kNumFrags); // minimal friction
		const double accelV = rnd(kNumFrags) * (isShip ? 0 : 10); // some gravity
		const CVector a(accelH, accelV);
		
		// give each fragment a random expiration time
		const int32_t lifetime = (isShip ? 4000 : 2000) + (300 * (rnd(kNumFrags)));
		
		this->NewObject(isShip ? eShipFragment : eFragment, {pos, v, a, lifetime, 0});
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	CalcPosition
//   - apply acceleration to velocity and velocity to position
//  tbarram 4/30/17
/*---------------------------------------------------------------------------*/
void CObject::CalcPosition(const double diffSec)
{
	if (this->IsFixed())
		return;
	
	if (this->IsDockedToEarth())
	{
		CObject* f = mPongView->GetFlatEarthObject();
		CMN_ASSERT(f);
		mState.mPos = f->FlatEarthDockPoint();
		mState.mVel = f->Vel();
		mState.mAcc = f->Acc();
		return;
	}
	
	if (mHasFriction)
	{
		// switch horiz acceleration so it acts like friction
		const double adjustedAccel = mState.mVel.mX > 0 ? -1 : 1;
		mState.mAcc.mX *= (adjustedAccel);
	}
	
	// apply acceleration to velocity
	// not sure why this single line doesn't work
	//mState.mVel += (mState.mAcc * diffSec);
	mState.mVel.mX += (mState.mAcc.mX * diffSec);
	mState.mVel.mY += (mState.mAcc.mY * diffSec);
	
	if (mBoundVelocity)
	{
		if (fabs(mState.mVel.mX) < 1.0)
			mState.mVel.mX = 0.0; // clamp to zero when it gets close to avoid jitter
		
		// bound velocity - needed?
		Bound(mState.mVel.mX, -1000, 1000);
		Bound(mState.mVel.mY, -1000, 1000);
	}
	
	if (this->Is(eBullet))
	{
		// calc previous position rects for bullet collisions - need these so
		// the bullets don't pass through objects due to low frame rate -
		// i.e. CCD (Continuous Collision Detection)
		const double diffSecInc = diffSec / (double)kNumPreviousPositions;
		for (int32_t k = 0; k < kNumPreviousPositions; k++)
		{
			const double inc = diffSecInc * k;
			const double v = mState.mPos.mY + (mState.mVel.mY * inc);
			const double h = mState.mPos.mX + (mState.mVel.mX * inc);
			mPreviousRect[k] = CRect(h, v, h + mWidth, v + mHeight);
		}
	}

	// apply velocity to position
	//mState.mPos += (mState.mVel * diffSec);
	mState.mPos.mX += (mState.mVel.mX * diffSec);
	mState.mPos.mY += (mState.mVel.mY * diffSec);
	
	// collision rect - based on top left
	mRect = CRect(mState.mPos.mX, mState.mPos.mY, mWidth, mHeight);
	
	if (this->WrapsHorizontally())
	{
		const int32_t gridWidth = mPongView->GetGridWidth();
		if (mState.mPos.mX > gridWidth)
			mState.mPos.mX = 0;
		else if (mState.mPos.mX < 0)
			mState.mPos.mX = gridWidth;
	}
		
	if (this->Is(eShip))
	{
		// when ship hits ground, set vertical velocity & accel back to 0
		if (this->IsOnGround())
		{
			mState.mVel.mY = 0;
			
			// setting this to zero is interesting - figure out a way to make it
			// part of the game - this removes gravity and allows you to hover
			if (kNoGravity)
				mState.mAcc.mY = 0;
		}
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	CollidedWith
//  tbarram 1/26/18
/*---------------------------------------------------------------------------*/
bool CObject::CollidedWith(CObject& other)
{
	if (this->Is(eBullet) && other.Is(eBullet))
	{
		CMN_ASSERT(!"remnove this now that we check for KilleBy before getting here");
		return false;
	}
	
	// handle bullets
	CObject* bullet = nullptr;
	CObject* nonBullet = nullptr;
	if (this->Is(eBullet))
	{
		bullet = this;
		nonBullet = &other;
	}
	else if (other.Is(eBullet))
	{
		bullet = &other;
		nonBullet = this;
	}
	
	if (bullet)
	{
		for (int32_t k = 0; k < kNumPreviousPositions; k++)
			if (this->CollidedWith(bullet->mPreviousRect[k], nonBullet->mRect))
				return true;
	}
	else
	{
		return this->CollidedWith(this->mRect, other.mRect);
	}
	
	return false;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	CollidedWith
/*---------------------------------------------------------------------------*/
bool CObject::CollidedWith(CRect& a, CRect& b)
{
	return !a.getIntersection(b).isEmpty();
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	IsUnderLine
/*---------------------------------------------------------------------------*/
bool CObject::IsUnderLine(CVector right, CVector left, CVector pt)
{
	// only check the segment we're in
	if (pt.mX < left.mX || pt.mX > right.mX)
		return false;
	
	return (((left.mX - right.mX) * (pt.mY - right.mY)) - ((left.mY - right.mY) * (pt.mX - right.mX))) <= 0;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	CollidedWithGround
/*---------------------------------------------------------------------------*/
bool CObject::CollidedWithGround(CObject& ground, CObject& obj)
{
	const std::vector<Cmn_Point32> vertices = obj.GetVertices();
	for (const auto& v : vertices)
		if (IsUnderLine(ground.mRightPoint, ground.mLeftPoint, CVector(v.x, v.y)))
			return true;
		
	return false;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	Collided
//  tbarram 5/5/17
/*---------------------------------------------------------------------------*/
void CObject::Collided(ECollisionType type)
{
	if (this->Is(eBullet))
	{
		mHitPoints = 0;
		return;
	}
	
	if (type == eSmart || type == eWithGround || this->Is(eChaser))
		mHitPoints = 1;
	
	if (mHitPoints > 0 && --mHitPoints == 0)
	{
		if (!mPongView->LevelPause())
		{
			// blow us up
			mPongView->Explosion(mState.mPos, this->Is(eShip));
		
			// keep stats
			if (this->Is(eIcon) || this->Is(eVector))
				mPongView->AddKill();
			
			else if (this->Is(eShip))
			{
				mPongView->AddDeath();
				this->ShipDestroyed();
			}
		}
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	Rotate
//  - rotate point p around a center point c based on the Ship's mAngle
//  tbarram 4/29/17
/*---------------------------------------------------------------------------*/
void CObject::Rotate(CPoint& p, const CPoint& c)
{
	// normalize
	p.x -= c.x;
	p.y -= c.y;

	// rotate
	const double h = p.x * mAngleCos - p.y * mAngleSin;
	const double v = p.x * mAngleSin + p.y * mAngleCos;

	// un-normalize
	p.x = h + c.x;
	p.y = v + c.y;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	AnimateShip
//  tbarram 4/29/17
/*---------------------------------------------------------------------------*/
void CObject::AnimateShip()
{
	if (kFreezeShipInMiddle)
		mState.mPos = {600, 400};
	
	// position refers to the center of the triangle
	CPoint pos(mState.mPos.mX, mState.mPos.mY);
	
	static const int32_t kBaseWidth = 16;
	static const int32_t kHeight = 8; //24;
	static const int32_t kHalfBaseWidth = kBaseWidth / 2;
	static const int32_t kHalfHeight = kHeight / 2;
	static const int32_t kCenterIndent = 4;
	static const int32_t kThrustWidth = (kBaseWidth / 4) - 1;
	static const int32_t kThrustHeight = 8; //kHalfHeight - 2;
	
	// cache mFront for bullet origin
	mFront = CPoint(pos.x, pos.y - kHalfHeight);
	this->Rotate(mFront, pos);
	
	std::list<CPoint> ship;
	ship.push_back(CPoint(pos.x - kHalfBaseWidth, pos.y + kHalfHeight)); // bottomL
	ship.push_back(CPoint(pos.x, pos.y + kHalfHeight - kCenterIndent)); // bottomC
	ship.push_back(CPoint(pos.x + kHalfBaseWidth, pos.y + kHalfHeight)); // bottomR
	ship.push_back(CPoint(pos.x, pos.y - kHalfHeight));	// top
	
	// mVertices is used for drawing and for collision-with-ground detection
	mVertices.clear();
	mVertices.reserve(ship.size());
	
	// rotate each point and add to mVertices
	for (auto& pt : ship)
	{
		this->Rotate(pt, pos);
		mVertices.push_back({(int)pt.x, (int)pt.y});
	}
	
	// calc the collision rect (overrides the one calculated in CalcPosition)
	// (I couldnt get p.GetBoundingBox() to work due to compile errors,
	// so I copied its impl here)
	int32_t maxH, minH, maxV, minV;
	maxH = minH = mVertices.front().x;
	maxV = minV = mVertices.front().y;
	
	for (const auto& iter : mVertices)
	{
		maxH = Cmn_Max(maxH, iter.x);
		minH = Cmn_Min(minH, iter.x);
		maxV = Cmn_Max(maxV, iter.y);
		minV = Cmn_Min(minV, iter.y);
	}
	
	mRect = CRect(minH, minV, maxH, maxV); //l,t,r,b
	
	mThrustVertices.clear();
	if (mThrusting)
	{
		std::list<CPoint> thrust;
		thrust.push_back(CPoint(pos.x - kThrustWidth, pos.y + kHalfHeight)); // bottomL
		thrust.push_back(CPoint(pos.x, pos.y + kHalfHeight + kThrustHeight)); // bottomC
		thrust.push_back(CPoint(pos.x + kThrustWidth, pos.y + kHalfHeight)); // bottomR
		
		mThrustVertices.reserve(thrust.size());
		
		// rotate each point and add to mThrustVertices
		for (auto& pt : thrust)
		{
			this->Rotate(pt, pos);
			mThrustVertices.push_back({(int)pt.x, (int)pt.y});
		}
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	DrawShip
//  tbarram 4/29/17
/*---------------------------------------------------------------------------*/
void CObject::DrawShip(Graphics& g)
{
	g.setColour(Colours::lawngreen);
	
	Path path;
	path.startNewSubPath(Point<float> (mVertices[0].x, mVertices[0].y));
	path.lineTo (Point<float> (mVertices[1].x, mVertices[1].y));
	path.lineTo (Point<float> (mVertices[2].x, mVertices[2].y));
	path.lineTo (Point<float> (mVertices[3].x, mVertices[3].y));
	path.closeSubPath();
	g.fillPath (path);
	
	// draw thrust
	if (mThrustVertices.size() > 0)
	{
		Path t;
		t.addTriangle(mThrustVertices[0].x, mThrustVertices[0].y, mThrustVertices[1].x, mThrustVertices[1].y, mThrustVertices[2].x, mThrustVertices[2].y);
		g.setColour(Colours::red); 
		g.fillPath(t);
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	GetControlData
//   - check the keyboard state and update
//  tbarram 4/29/17
/*---------------------------------------------------------------------------*/
void CObject::GetControlData()
{
	if (KeyPress::isKeyCurrentlyDown(KeyPress::rightKey) ||
				KeyPress::isKeyCurrentlyDown('d'))
		mAngle += kRotateSpeed;
	
	if (KeyPress::isKeyCurrentlyDown(KeyPress::leftKey) ||
				KeyPress::isKeyCurrentlyDown('a'))
		mAngle -= kRotateSpeed;
	
	// keep the angle under 2PI
	mAngle = ::fmodf(mAngle, 2*PI);
	
	// clamp at 0 when it gets close so ship gets truly flat
	if (::fabs(mAngle) < 0.0001)
		mAngle = 0.0;
	
	// calc and cache sin & cos
	mAngleSin = ::sin(mAngle);
	mAngleCos = ::cos(mAngle);
	
	mThrusting = false;
	if (KeyPress::isKeyCurrentlyDown('z') ||
		KeyPress::isKeyCurrentlyDown('w') ||
		KeyPress::isKeyCurrentlyDown(KeyPress::upKey))
	{
		// un-lock from earth when thrust happens after the initial wait
		if (this->IsDockedToEarth() && gNowMS > mDockedToEarthMS)
		{
			mDockedToEarthMS = 0;
			mState.mVel = {0, -20};
			mState.mAcc = {20, 80}; // reset acceleration
		}
		
		if (!this->IsDockedToEarth())
		{
			mState.mVel.mY -= (mAngleCos * kThrustSpeed); // vertical thrust
			mState.mVel.mX += (mAngleSin * kThrustSpeed); // horiz thrust
			mThrusting = true;
		}
	}
	
	// in case the ship gets too far off the screen
	if (mPongView->CheckKeyPress('q', 1000))
		this->ShipDestroyed();
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	LineBetween - util
/*---------------------------------------------------------------------------*/
void CObject::LineBetween(Graphics& g, CVector p1, CVector p2, int32_t size)
{
	g.drawLine(p1.mX, p1.mY, p2.mX, p2.mY, size);
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	DrawGroundObject
//   - draw one segment of the ground
//  tbarram 3/29/17
/*---------------------------------------------------------------------------*/
void CObject::DrawGroundObject(Graphics& g)
{
	g.setColour(Colours::lawngreen);
	
	// the position of the line segment is its left endpoint
	mLeftPoint = mState.mPos;
	mRightPoint = {mLeftPoint.mX + mWidth, mLeftPoint.mY + mHeight};
	
	this->LineBetween(g, mLeftPoint, mRightPoint);
	
	// draw the ground in the minimap
	//CVector miniMapL = TranslateForMinimap(mLeftPoint);
	//CVector miniMapR = TranslateForMinimap(mRightPoint);
	//this->LineBetween(g, miniMapL, miniMapR, 1);
	
	// when this line segment's right side hits the right edge, create the next one
	if (mRightPoint.mX <= mPongView->GetGridWidth() && !mHasTriggeredNext)
	{
		mHasTriggeredNext = true;
		
		// start next object
		mPongView->NewGroundObject(mRightPoint);
	}
}

/*---------------------------------------------------------------------------*/
void CObject::SetImage(Image* img)
{
	mImage = img;
	CMN_ASSERT(mImage->isValid());
	mWidth = mImage->getWidth();
	mHeight = mImage->getHeight();
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	Init
//  tbarram 4/30/17
/*---------------------------------------------------------------------------*/
void CObject::Init()
{
	bool usesImage = false;
	
	switch (mType)
	{
		case eShip:
			//mPastPositions.reserve(sPastPositionMax);
			return;
			
		case eFlatEarth:
		{
			mImage = &mPongView->GetFlatEarthImage();
			usesImage = true;
			break;
		}
		case eGround:
		{
			static bool sIncreasingSlope = true;
			
			// each ground object is a new line segment
			// get random values for the width and height of this line segment
			mWidth = rnd(30, 120);
			int32_t height = rnd(10, 100);
			
			// make sure the line segments stay within a reasonable range
			static const int32_t kMinY = (mPongView->GetGridHeight() - 300);
			static const int32_t kMaxY = (mPongView->GetGridHeight() - 30);

			if (sIncreasingSlope && (height > (mState.mPos.mY - kMinY)))
				height = mState.mPos.mY - kMinY;
			else if (!sIncreasingSlope && (height > (kMaxY - mState.mPos.mY)))
				height = kMaxY - mState.mPos.mY;
			
			// switch increasing & decreasing
			mHeight = height * (sIncreasingSlope ? -1.0 : 1.0);
			sIncreasingSlope = !sIncreasingSlope;
			
			// set this non-zero so the object doesn't immediately die in IsAlive()
			mRightPoint.mX = 1;
			return;
		}
		case eBullet: // bullets are red
		{
			mColor = Colours::red;
			const int32_t size = mPongView->GetShipObject()->IsDockedToEarth() ? 6 :4;
			mWidth = mHeight = size;
			break;
		}
		case eFragment: // frags are green (native color of the icon)
		{
			const int32_t kNumColors = 2;
			Colour c[kNumColors] = {Colours::lawngreen, Colours::ivory};
			mColor = c[rnd(kNumColors)];
			mWidth = mHeight = rnd(2, 6);
			break;
		}
		case eShipFragment:
		{
			const int32_t kNumColors = 5;
			Colour c[kNumColors] = {Colours::lawngreen, Colours::ivory, Colours::blue, Colours::orange, Colours::yellow};
			mColor = c[rnd(kNumColors)];
			mWidth = mHeight = rnd(2, 6);
			break;
		}
		case eIcon:
		case eVector:
		{
			// get a random image from the mImages vector
			auto it = mPongView->GetImages().begin();
			std::advance(it, rnd((int32_t)mPongView->GetImages().size()));
			mImage = &(*it);
			usesImage = true;
			break;
		}
		case eChaser:
		{
			mImage = &mPongView->GetChaserImage();
			usesImage = true;
			break;
		}
		case eMiniMap:
		{
			if (mColor == (Colour)0)
			{
				const int32_t kNumColors = 3;
				Colour c[kNumColors] = {Colours::ivory, Colours::blue, Colours::yellow};
				mColor = c[rnd(kNumColors)];
			}
			//mColor = Colours::black;
			mWidth = mHeight = 2;
			break;
		}
		default:
			break;
	}
	
	if (usesImage)
	{
		CMN_ASSERT(mImage->isValid());
		mWidth = mImage->getWidth();
		mHeight = mImage->getHeight();
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	Animate
//  tbarram 4/30/17
/*---------------------------------------------------------------------------*/
void CObject::Animate(const double diffSec)
{
	if (this->Is(eShip))
		this->GetControlData();
	
	if (this->Is(eVector))
		this->VectorCalc(diffSec);
	
	// update position & velocity
	this->CalcPosition(diffSec);
	
	// do special ship animation (rotate, etc)
	if (this->Is(eShip))
	{
		mPongView->AddChaserPosition(mState.mPos);
		this->AnimateShip();
	}
	
	if (this->Is(eChaser))
		this->AnimateChaser();
	
	if (this->Is(eMiniMap))
		this->AnimateMiniMapObject();
	
	mNumAnimates++;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	Draw
//  tbarram 4/30/17
/*---------------------------------------------------------------------------*/
void CObject::Draw(Graphics& g)
{
	// skip the first bullet draw so it doesn't get offset from the front of the ship
	if (this->Is(eBullet) && mNumAnimates == 0)
		return;
	
	if (this->Is(eShip))
	{
		return this->DrawShip(g);
	}
	else if (this->Is(eGround))
	{
		return this->DrawGroundObject(g);
	}
	
	if (mImage && mImage->isValid())
	{
		CRectF fr(mState.mPos.mX, mState.mPos.mY, mWidth, mHeight);
		g.setOpacity(1.0f);
		g.drawImage(*mImage, fr);
	}
	else
	{
		g.setColour(mColor);
		g.fillEllipse(mState.mPos.mX, mState.mPos.mY, mWidth, mHeight);
	}

	// for collision with ground - just center for now
	mVertices.push_back({(int32_t)mState.mPos.mX, (int32_t)mState.mPos.mY});
}

/*---------------------------------------------------------------------------*/
void CObject::AnimateChaser()
{
	mState.mPos = mPongView->GetChaserPosition();
}

/*---------------------------------------------------------------------------*/
void CObject::AnimateMiniMapObject()
{
	mState.mPos = TranslateForMinimap(mParent->Pos());
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	AddVectorPathElement
//   1 VectorPathElement maps to 2 VectorPoints - should clean this up and make
//    the required changes in VectorCalc to handle VPEs instead of VectorPoints
/*---------------------------------------------------------------------------*/
void CObject::AddVectorPathElement(VectorPathElement vpe)
{
	mVectorPath[mNumVectorPoints++] = VectorPoint(vpe.mPos, vpe.mMoveTime); 	// move to pos
	mVectorPath[mNumVectorPoints++] = VectorPoint(vpe.mPos, vpe.mPauseTime);	// pause at pos
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	VectorCalc
//  tbarram 5/5/17
/*---------------------------------------------------------------------------*/
void CObject::VectorCalc(const double diffSec)
{
	if (!this->GetNumVectorPoints())
		return;
	
	const VectorPath& path = this->GetVectorPath();
	
	// see if it's time to switch to the next point
	if (gNowMS - mLastVectorPointMS > path[mVectorIndex].second)
	{
		mLastVectorPointMS = gNowMS;
		
		const int32_t currentIndex = mVectorIndex % this->GetNumVectorPoints();
		const int32_t nextIndex = (mVectorIndex + 1) % this->GetNumVectorPoints();
		const bool isFirst = (mVectorIndex == 0);
		mVectorIndex++;
		
		// once we've started moving, use the actual current point instead of the expected one -
		// for some reason the math is missing the target - maybe the amount of time is off by 1?
		if (isFirst)
			mState.mPos = path[currentIndex].first;
		
		const CVector nextPos = path[nextIndex].first;
		
		if (nextPos.mY == mState.mPos.mY && nextPos.mX == mState.mPos.mX)
		{
			mState.mVel.mX = mState.mVel.mY = 0;
		}
		else
		{
			const double distance = Distance(nextPos, mState.mPos);
			double speed = distance * 1000 / (double)path[nextIndex].second;
			if (speed < 4)
				speed = 0;
			
			// the velocity angle to get to the next point
			const double angle = ::atan2(nextPos.mY - mState.mPos.mY, nextPos.mX - mState.mPos.mX);
			
			mState.mVel.mX = speed * ::cos(angle);
			mState.mVel.mY = speed * ::sin(angle);
		}
	}
}

