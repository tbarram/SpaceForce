// copyright (c) 1990-2017 by Digidesign, Inc. All rights reserved. 

/*****************************************************************************

	SpaceForce.cpp
	
	The game is designed to have a minimal footprint - once the intial TPongView
	object is created, there are no additional heap allocations - all new
	Objects are created on the stack from a pre-allocated memory pool (using
	placement new to get the benefits of the usual constructor logic.)
	
	Ted Barram 4/29/17
*****************************************************************************/

#include "SpaceForce.h"
#include <list>
#include <map>
#include <math.h>

/*---------------------------------------------------------------------------*/


// mMonteum = how much stuff is happening - a generic 1 number measurement
// then you  casn m aoe siure you don't quit a game right when people get back involved
// other good uss too

const bool kDrawCollisionRectOutline = false; // for debugging

const bool kNoObjects = false; // set true if you just want to fly around with no distractions
const double kGroundSpeed = 150;
const bool kUseChaserObject = false;

const bool kUseIntroScreens = false;
const bool kDoDistanceGame = false;
const bool kDoHostageRescueGame = true;


enum DistanceGameStatus
{
	eInactive = 0,
	eWaitingForStart,
	eStarted,
	eActive
};

int32_t kHostageRescueGameLifeCounter = 0;
const int32_t kHostageRescueGameNumLives = 3;
int32_t kBestHostageGameScore = 0;
int32_t	mNumHostagesSaved = 0;
int32_t mScore = 0;
int64_t	mHostageGameStartTimeMS = 0;

// make a one-finger game where only thrust works
const int32_t kDistanceGameScoreCutoff = 44; // 38 seems best

// this caps the negative points as you get higher
// this makes thigns subtly worse let's disable it
const int32_t kDistanceGameScoreMaxPenalty = 20; //100;

const int32_t kDistanceGameScoreStartingPoints = 5000;
const int32_t kDistanceGameRotationBonus = 2000;
const int32_t kIntervalBetweenGames = 1000;

// tweak these for performance and sensitivity of controls
const int32_t kRefreshRateMS = 30;
const int32_t kAnimateThrottleMS = 10;
const double kRotateSpeed = M_PI/20;
const double kThrustSpeed = 20;
const bool kFreezeShipInMiddle = false;

const int32_t kGroundMidpoint = 300;

// minimap settings
const int32_t kMinimapHeight = 30;
const int32_t kMinimapOuterRatio = 4;
const std::pair<int32_t, int32_t> kMiniMapTopLeftCorner = {80, 60};

// pre-baked path for intro screen or replays
int32_t gHistoryIndex = 0;
const int32_t kHistorySize = 1000;

using CRect = Rectangle<int32_t>; // x,y,w,h
using CPointI = Point<int32_t>;
using CPointF = Point<float>;

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

const String kImagesFolder = File::getSpecialLocation(File::currentApplicationFile).getFullPathName() + "/Contents/Resources/Images/";
const String kSpecialImagesFolder = File::getSpecialLocation(File::currentApplicationFile).getFullPathName() + "/Contents/Resources/Images/Special/";
const String kGravityImagesFolder = File::getSpecialLocation(File::currentApplicationFile).getFullPathName() + "/Contents/Resources/Images/Gravity/";
	
const String cFlatEarthImagePath = kSpecialImagesFolder + "vibe_meter_glow_48.png";
const String cChaserImagePath = kSpecialImagesFolder + "bomb.png";
const String cHostageImagePath_Soldier = kSpecialImagesFolder + "icons8-standing-man-32.png";
const String cHostageImagePath_Boss = kSpecialImagesFolder + "icons8-cylon-head-new-24.png";
const String cHostageImagePath_Friend = kSpecialImagesFolder + "icons8-spy-32.png";
const String cBulletImagePath = kSpecialImagesFolder + "icons8-bang-12.png";
	
const int32_t kGridHeight = 800;
const int32_t kGridWidth = 1200;
	
bool mWasRotating = false;
double mAngleStart = 0;
bool mHitHalfwayMark = false;
int64_t mShipBlinkEndMS = 0;
int32_t mNumRotations = 0;
Colour mShipBlinkColor;
bool sIncreasingSlopeBottom = true;
bool sIncreasingSlopeTop = true;
int64_t mShipSafeEndMS = 0;
	
enum ScoringEvent
{
	eRescuedHostage 		= 1 << 0,
	eRescuedHostage2 		= 1 << 1,
	eRescuedHostage3 		= 1 << 2,
	eSingleRotate 			= 1 << 3,
	eDoubleRotate 			= 1 << 4,
	eTripleRotate 			= 1 << 5,
	eGroundCollision		= 1 << 6
};
	
std::map<ScoringEvent, bool> sScoreEventHaveShownText;
	
const int32_t kNumSamples = 20; // ~1 sec
class SlidingAverage
{
public:
	SlidingAverage() :
		mNumSamples(0),
		mTotal(0)
	{}
	
	void AddSample(int32_t sample)
	{
		if (mNumSamples < kNumSamples)
		{
			mSamples[mNumSamples++] = sample;
			mTotal += sample;
		}
		else
		{
			int32_t& oldest = mSamples[mNumSamples++ % kNumSamples];
			mTotal += (sample - oldest);
			oldest = sample;
		}
	}
	
	void Reset()
	{
		mNumSamples = 0;
		mTotal = 0;
	}
	
	int32_t Average() const
	{
		return mNumSamples ? mTotal / std::min(mNumSamples, kNumSamples) : 0;
	}
	
	int32_t NumSamples() const { return mNumSamples; }
	
private:
	int32_t mSamples[kNumSamples];
	int32_t mNumSamples;
	int32_t mTotal;
};

SlidingAverage gSlidingAverage;
	
/*---------------------------------------------------------------------------*/
class StFontRestorer
{
public:
	StFontRestorer(Font newFont, Graphics& g) :
		mG(g),
		mNewFont(newFont),
		mCurrentFont(g.getCurrentFont())
	{
		mG.setFont(newFont);
	}
	~StFontRestorer()
	{
		mG.setFont(mCurrentFont);
	}
	
private:
	Graphics& mG;
	const Font mNewFont;
	const Font mCurrentFont;
};

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
	
/*---------------------------------------------------------------------------*/
void BoundLo(int64_t& val, const int64_t low)
{
	if (val < low)
		val = low;
}
	
/*---------------------------------------------------------------------------*/
void BoundHi(int64_t& val, const int64_t hi)
{
	if (val > hi)
		val = hi;
}

// update once every wakeup
int64_t gNowMS = 0;
int64_t gStartTimeMS = 0;
	
/*---------------------------------------------------------------------------*/
bool CheckDeadline(int64_t targetMS)
{
	return targetMS && gNowMS > targetMS;
}

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
	
	CVector operator-(const CVector& rhs)
	{
		return CVector(mX - rhs.mX, mY - rhs.mY);
	}
	
	CVector operator+(const CVector& rhs)
	{
		return CVector(mX + rhs.mX, mY + rhs.mY);
	}
	
	bool operator==(const CVector& rhs)
	{
		return mX == rhs.mX && mY == rhs.mY;
	}
	
	// takes a raw angle, or the pre-computed sin & cos
	static CVector Velocity(const double speed, const double angle, std::pair<double, double> trig = {0.0f,0.0f})
	{
		const double sin_ = trig.first != 0 ? trig.first : ::sin(angle);
		const double cos_ = trig.second != 0 ? trig.second : ::cos(angle);
		return CVector(speed * sin_, -(speed * cos_));
	}
};
	
const CVector zero(0,0);
	
/*---------------------------------------------------------------------------*/
double DistanceSq(const CVector& p1, const CVector& p2)
{
	return (std::pow((p1.mX - p2.mX), 2) + std::pow((p1.mY - p2.mY), 2));
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
	
	// mExpireTimeMS & mKilledBy should be moved to members of CObject
	// CState should only represent position / physics
	
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
	eHostage		= 1 << 11,
	eTextBubble		= 1 << 12,
	eAll			= 0xFFFF
};
	
/*---------------------------------------------------------------------------*/
enum EHostageType
{
	eSoldier	= 0,
	eBoss		= 1,
	eFriend		= 2
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

// minimap
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
	
/*---------------------------------------------------------------------------*/
struct ObjectHistory
{
	ObjectHistory(int32_t x, int32_t y, double angle, bool thrusting) :
		mX(x), mY(y), mAngle(angle), mThrusting(thrusting)
	{}
	
	void AddSample(int32_t x, int32_t y, double angle, bool thrusting)
	{
		mX = x;
		mY = y;
		mAngle = angle;
		mThrusting = thrusting;
	}
	
	// for capturing ship movements
	static std::vector<ObjectHistory> gShipHistory;
	
	// the data from the capture
	static std::vector<ObjectHistory> gPredefinedShipPath;
	
	static void LogHistory()
	{
		for (int k = 0; k< kHistorySize; k++)
			gShipHistory[k].Log();
	}
	
	// the format of this print statement is such that you can copy/paste it
	// and use it to init the gPredefinedShipPath vector
	// (see the gPredefinedShipPath init at the bottom of this file)
	void Log()
	{
		printf("ObjectHistory(%d,%d,%.8lf,%d),\n",
			   mX, mY, mAngle, mThrusting ? 1 : 0);
	}
	
	int32_t mX;
	int32_t mY;
	double mAngle;
	bool mThrusting;
};
	
bool gUsePredefinedShipPath = false;
int32_t gShipPathIndex = 0;
std::vector<ObjectHistory> ObjectHistory::gShipHistory;

	
} // pong namespace

using namespace pong;
class TPongView;
TPongView* mPongView;

// CObject
class CObject
{
public:

	CObject() :
		mType(eNull),
		mInUse(false)
	{}
	
	CObject(TPongView* pongView, const EObjectType type, const CState state) :
		mType(type),
		mState(state),
		mHitPoints(1),
		mReadyAfterMS(0),
		mNumAnimates(0),
		mReady(true),
		mHasFriction(true),
		mIsFixed(false),
		mBoundVelocity(true),
		mThrustEnabled(true),
		mColor(0),
		mMass(0),
		mGroundObjectForHostage(nullptr),
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
		mPongView = pongView;
		this->Init();
	}
	
	// this only gets called when the pool goes away - it does not get called
	// when an object gets released back into the pool
	virtual ~CObject() {}
	
	void		Animate(const double diffSec);
	void		AnimateShip();
	void		GetPredefinedShipData();
	void		AnimateChaser();
	void		AnimateMiniMapObject();
	void		Draw(Graphics& g);
	void		DrawShip(Graphics& g);
	void		DrawGroundObject(Graphics& g);
	void		DrawTextBubble(Graphics& g);
	void		DrawBullet(Graphics& g);
	void 		InitGround(bool isBottom);
	
	EObjectType		Type() const { return mType; }
	
	void			CalcPosition(const double diffSec);
	void			VectorCalc(const double diffSec);
	static bool		CollidedWith(CRect& a, CRect& b);
	bool			CollidedWith(CObject& other);
	static bool		CollidedWithGround(CObject& ground, CObject& obj);
	static bool		IsUnderLine(CVector right, CVector left, CVector pt);
	static bool		IsAboveLine(CVector right, CVector left, CVector pt);
	static int32_t	CalcDistanceToGround(CObject& ground, CObject& obj);
	static int32_t	DistanceToLine(CVector right, CVector left, CVector pt);
	static void		LineBetween(Graphics& g, CVector p1, CVector p2, int32_t size = 2);
	void			SetNumHitPoints(int32_t hits) { mHitPoints = hits; }
	int32_t			GetNumHitPoints() const { return mHitPoints; }
	void			SetDockedToEarth() { mDockedToEarthMS = gNowMS + 1000; }
	bool			IsDockedToEarth() const { return mDockedToEarthMS != 0; }
	void			Collided(ECollisionType type);
	int32_t			GetKilledBy() const { return mState.mKilledBy; }
	bool			IsKilledBy(EObjectType type) { return mState.mKilledBy & type; }
	void			ShipReset();
	void			EnableThrust(bool enabled) { mThrustEnabled = enabled; }
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
	void			SetFixed(bool fixed) { mIsFixed = fixed; }
	void		 	SetTextBubbleTest(std::string text) { mTextBubbleText = text; }
	bool			IsFixed() { return mIsFixed; }
	void			SetColor(Colour color) { mColor = color; }
	void			SetWidthAndHeight(int32_t w, int32_t h) { mWidth = w; mHeight = h; }
	void			SetImage(Image* img);
	void			SetParent(CObject* parent){ mParent = parent; mParent->SetChild(this); }
	void			SetChild(CObject* child) { mChild = child; }
	CObject*		GetChild() const { return mChild; }
	CObject*		GetParent() const { return mParent; }
	void 			SetGroundObjectForHostage(CObject* obj) { mGroundObjectForHostage = obj; }
	void			SetHostageType(EHostageType type) { mHostageType = type; };
	void			SetHostageOffset(const CVector& offset) { mHostageOffset = offset; }
	bool 			IsOffscreen() const { return (mState.mPos.mY > kGridHeight || mState.mPos.mY < 0 ||
												  mState.mPos.mX > kGridWidth || mState.mPos.mX < 0); }
	
	bool IsBottom() const { return mIsBottom; }
	
	// a point 25 pixels above the center
	CVector	FlatEarthDockPoint() const { return CVector(mState.mPos.mX, mState.mPos.mY - 25); }
	
	// for ship object
	void			Rotate(CPointF& p, const CPointF& c);
	void			GetControlData();
	void			CheckRotation(bool isRotating);
	double			GetAngle() const { return mAngle; }
	double			GetSin() const { return mAngleSin; }
	double			GetCos() const { return mAngleCos; }
	CVector			GetFront() const { return {(double)mFront.x, (double)mFront.y}; }
	std::vector<CPointI> GetVertices() { return mVertices; }
	void SetDistanceFrtomGround(int32_t d) { mDistanceFromGround = d; }
	
	// stop gravity if the ship is on the ground
	bool 			IsOnGround() const { return (mState.mPos.mY >= (kGridHeight - 50)); }
	
	// for vector objects
	VectorPath&	GetVectorPath() { return mVectorPath; }
	void		AddVectorPathElement(VectorPathElement vpe);
	int32_t		GetNumVectorPoints() const { return mNumVectorPoints; }
	
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
		return (radians * 180 * M_1_PI);
	}
	
	// used in pool and ready
	bool IsActive() const { return this->InUse() && this->IsReady(); }
	
	// release the object's resources, and mark the object slot as available -
	// we need to do this explicitly since the destructor doesn't get called
	// since the objects are coming from a pool
	void Free() { mInUse = false; }
	
protected:
	const EObjectType			mType;
	CState						mState;
	int32_t						mHitPoints;
	int64_t						mReadyAfterMS;
	std::vector<CPointI>	mVertices;
	int32_t						mNumAnimates;
	bool						mReady;
	bool						mHasFriction;
	bool						mIsFixed;
	bool						mBoundVelocity;
	bool						mThrustEnabled;
	Colour						mColor;
	double						mMass;
	int32_t						mDistanceFromGround;
	CObject* 					mGroundObjectForHostage;
	CVector						mHostageOffset;
	EHostageType				mHostageType;
	int32_t 					mRangeMinY;
	int32_t 					mRangeMaxY;
	bool						mIsBottom;
	
	CRect						mCollisionRect;
	
	// only used by bullets to support CCD (Continuous Collision Detection)
	#define						kNumBulletCollisionRects 8
	CRect						mBulletCollisionRects[kNumBulletCollisionRects];
	
private:
	void Init();
	
	// image data
	Image* mImage;
	std::string mImageName;
	
	// for TextBubble object
	std::string mTextBubbleText;
	
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
	CPointF		mFront;
	std::vector<CPointI> mThrustVertices;
	bool		mThrusting;
	int64_t		mDockedToEarthMS;
	
	// for vector objects
	int32_t				mVectorIndex;
	int32_t				mNumVectorPoints;
	int64_t				mLastVectorPointMS;
	VectorPath	mVectorPath;
	
	// for ground objects
	CVector	mLeftEndpoint;
	CVector	mRightEndpoint;
	bool	mHasTriggeredNext;
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
		
		// go through the pool looking for ready objects
		for (int k = 0; k < kMaxNumObjects; k++)
		{
			CObject& obj = mPool[k];
			if (!obj.IsActive())
				continue;
			
			mNumActiveObjects++;
			
			// animate
			obj.Animate(diffSec);
			
			// if the object is now dead, remove it from the pool
			if (!obj.IsAlive())
			{
				obj.Died();	// object-specific cleanup
				obj.Free();	// free back into the ObjectPool
				
				if (obj.Is(eGround))
					mGroundObjectList.remove(&obj);
				
				// release this slot back into the pool (not thread safe)
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
	
	int32_t CalcShipDistanceToGround(CObject& ship)
	{
		int32_t distance = INT_MAX;
		
		// not very efficient to check every ground line segment - need
		// a map or something (but this INT_MAX mechanism works fine)
		for (const auto& g : mGroundObjectList)
		{
			const int32_t d = CObject::CalcDistanceToGround(*g, ship);
			if (d < distance)
				distance = d;
		}
		ship.SetDistanceFrtomGround(distance);
		return distance;
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
	void ApplyGravity(CObject& obj1, CObject& obj2);
	
private:
	CObject mPool[kMaxNumObjects];
	CObject* mFirstOpenSlot;
	std::list<CObject*> mGroundObjectList;
	int32_t mNumActiveObjects;
};


// TPongView
class TPongView : public IPongView
{
public:
	TPongView() :
		mShipObject(nullptr),
		mFlatEarthObject(nullptr),
		mChaserObject(nullptr),
		mLastDrawMS(0),
		mNextNewFallingIconObjectMS(0),
		mNextNewCrawlingIconObjectMS(0),
		mNextNewVectorIconObjectMS(0),
		mNextNewChaserObjectMS(0),
		mNextHostageObjectMS(0),
		mTotalNumHostages(0),
		mVectorObjectActive(false),
		mShowGuideEndMS(0),
		mOneTimeGuideExplosion(true),
		mShowLevelTextUntilMS(0),
		mNextDistanceGameStartTimeMS(0),
		mDistanceGameStartTimeMS(0),
		mDistanceGameDurationMS(0),
		mDistanceGameDurationBestMS(0),
		mLevel(0),
		mNextLevelKills(10000),
		mKills(0),
		mDeaths(0),
		mNumSmartBombs(4),
		mVectorCount(0),
		mShipGravity(!kUseIntroScreens),
		mAutoSmartBombMode(false),
		mIsPaused(false),
		mNumGravityObjects(0),
		mGravityObjectsEnabled(true),
		mBlackHoleEnabled(false),
		mFlatEarthEnabled(false),
		mIntroScreen(eIntro),
		mIntroScreenChanged(true),
		mIntroScreenChangedTimeMS(0),
		mDistanceGameStatus(kDoDistanceGame ? eActive : eInactive),
		mMusicCallback(nullptr),
		mRotaryCallback(nullptr)
	{}
	~TPongView() {}
	
	// public interface
	virtual void Draw(Graphics& g) override;
	virtual int32_t GetRefreshrateMS() override { return kRefreshRateMS; }
	virtual int32_t GetGridWidth() override { return kGridWidth; };
	virtual int32_t GetGridHeight() override { return kGridHeight; };
	virtual void SetSongName(std::string name) override { mSongName = name; }
	virtual void InstallMusicCallback(std::function<void()> f) override { mMusicCallback = f; }
	virtual void InstallRotaryCallback(std::function<void(int32_t)> f) override { mRotaryCallback = f; }
	virtual Colour ColorForScore(int32_t score) override;
	void Animate();
	void CheckKeyPresses();
	void CreateNewObjects();
	void UpdateLevel();
	void CheckDockedToEarth();
	void DoDistanceGame(Graphics& g);
	void DoHostageRescueGame(Graphics& g);
	void DoExplosions();
	bool CheckKeyPress(char key, int32_t throttleMS);
	void VectorObjectDied();
	void ChaserObjectDied();
	void HostageObjectDied();
	bool LevelPause() const { return mShowLevelTextUntilMS != 0; }
	int32_t GetGridWidth() const { return kGridWidth; }
	int32_t GetGridHeight() const { return kGridHeight; }
	CObjectPool& GetObjectPool() { return mObjectPool; }
	void AddKill() { mKills++; }
	void AddDeath() { mDeaths++; }
	void Explosion(const CVector& pos, bool isShip = false);
	CObject* GetFlatEarthObject() const { return mFlatEarthObject; }
	CObject* GetShipObject() const { return mShipObject; }
	void NewGroundObject(CVector pos, bool isBottom);
	CObject* NewObject(const EObjectType type, const CState state, bool minimap = false);
	std::vector<Image>& GetImages() { return mImages; }
	Image& GetFlatEarthImage() { return mFlatEarthImage; }
	Image& GetChaserImage() { return mChaserImage; }
	Image& GetHostageImage(EHostageType type) { return mHostageImage[type]; }
	Image& GetBulletImage() { return mBulletImage; }
	CVector& GetChaserPosition();
	void AddChaserPosition(CVector& vec);
	void CreateGravityObjects();
	void ToggleFlatEarthObject();
	bool ShipGravity() const { return mShipGravity; }
	bool DistanceGameActive() const { return mDistanceGameStatus != eInactive; }
	void SetDistanceGameScore(int32_t score);
	void Rotation(int32_t numRotations);
	void RescuedHostage(EHostageType type);
	void ClearHostages() { mTotalNumHostages = mNumHostagesSaved = mScore = 0; }
	void SetShipSafe(int64_t lengthMS);
	void ScoreEvent(ScoringEvent ev);
		
private:
	
	void			Init();
	void			Free();
	void			DrawText(Graphics& g);
	void			DrawIntroScreens(Graphics& g);
	void 			DrawIntroText(std::string text, Graphics& g, bool start = false);
	void 			DrawTextAtY(std::string text, int32_t y, Graphics& g);
	void			DrawDistanceMeter(Graphics& g);
	void			NewFallingIconObject();
	void			NewCrawlingIconObject();
	void			NewChaserObject();
	CObject*		NewGravityObject(CVector pos, double mass);
	void 			NewTextBubble(std::string text, CVector pos, Colour color);
	void			NewVectorIconObject();
	void			ShootBullet();
	void			SmartBomb();
	int32_t			ScoreForEvent(ScoringEvent ev) const;
	std::string 	TextForScoreEvent(ScoringEvent ev) const;
	
private:
	
	CObject*		mShipObject;
	CObject*		mFlatEarthObject;
	CObject*		mChaserObject;
	int64_t			mLastDrawMS;
	int64_t			mNextNewFallingIconObjectMS;
	int64_t			mNextNewCrawlingIconObjectMS;
	int64_t			mNextNewVectorIconObjectMS;
	int64_t			mNextNewChaserObjectMS;
	int64_t			mNextHostageObjectMS;
	int32_t			mTotalNumHostages;
	bool			mVectorObjectActive;
	int64_t			mShowGuideEndMS;
	bool			mOneTimeGuideExplosion;
	int64_t			mShowLevelTextUntilMS;
	int64_t			mNextDistanceGameStartTimeMS;
	int64_t			mDistanceGameStartTimeMS;
	int64_t			mDistanceGameDurationMS;
	int64_t			mDistanceGameDurationBestMS;
	int32_t			mLevel;
	int32_t			mNextLevelKills;
	int32_t			mKills;
	int32_t			mDeaths;
	int32_t			mNumSmartBombs;
	int32_t			mVectorCount;
	int32_t			mShipDistanceToGround;
	int32_t			mDistanceGameScore;
	int32_t			mIntroTextCurrentY;
	
	bool			mShipGravity;
	bool			mAutoSmartBombMode;
	bool			mIsPaused;
	int32_t    		mNumGravityObjects;
	bool			mGravityObjectsEnabled;
	bool			mBlackHoleEnabled;
	bool			mFlatEarthEnabled;
	
	enum IntroScreens
	{
		eIntro = 0,
		eAddGravity,
		eGravityScreen,
		eShooting1,
		eShooting2,
		eGravityObjects1,
		eGravityObjects2,
		eGravityObjects3,
		eDone1,
		eDone2
	};
	
	int32_t	mIntroScreen; // an int so I can increment
	bool mIntroScreenChanged;
	int64_t mIntroScreenChangedTimeMS;
	
	int32_t	mDistanceGameStatus;
	std::string mDistanceGameString;
	
	std::string				mSongName;
	std::function<void()> 	mMusicCallback;
	
	std::function<void(int32_t)> mRotaryCallback;
	
	static const int32_t sChaserPositionMax = 512;
	int32_t	mChaserPositionIndex;
	CVector mChaserPositions[sChaserPositionMax];
	
	std::vector<Image> 	mImages;
	std::vector<Image> 	mGravityImages;
	Image				mBlackHoleMinimapImage;
	Image				mBlackHoleImage;

	Image 				mFlatEarthImage;
	Image				mChaserImage;
	std::map<EHostageType, Image> mHostageImage;
	Image				mBulletImage;
	int32_t				mGravityIndex;
	std::map<char, int64_t> mLastKeyPressTimeMS;
	
	CObjectPool		mObjectPool;
	
	friend IPongView;
	typedef std::shared_ptr<TPongView> PongViewPtr;
};

int32_t mHostageGameStatus = (kDoHostageRescueGame ? eWaitingForStart : eInactive);

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
void LoadFilesFromFolder(const String& dir, std::vector<Image>& images)
{
	DirectoryIterator iter(File(dir), false);
	while (iter.next())
	{
		File file(iter.getFile());
		if (file.getFileExtension() == ".png")
		{
			const Image& img = ImageFileFormat::loadFrom(file);
			if (img.isValid())
				images.push_back(img);
		}
	}
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
	mShowGuideEndMS = gNowMS + 4000; // show the guide for 4 seconds
	mIntroScreenChangedTimeMS = kUseIntroScreens ? gNowMS + 8000 : 0;
	mNextDistanceGameStartTimeMS = gNowMS + kIntervalBetweenGames;
	mNextHostageObjectMS = gNowMS + 4000;
	
	mHostageImage[eSoldier] = ImageFileFormat::loadFrom(File(cHostageImagePath_Soldier));
	mHostageImage[eBoss] = ImageFileFormat::loadFrom(File(cHostageImagePath_Boss));
	mHostageImage[eFriend] = ImageFileFormat::loadFrom(File(cHostageImagePath_Friend));
	mBulletImage = ImageFileFormat::loadFrom(File(cBulletImagePath));
	
	ObjectHistory::gShipHistory.reserve(kHistorySize);
	
	mObjectPool.Init();
	LoadFilesFromFolder(kImagesFolder, mImages);
	LoadFilesFromFolder(kGravityImagesFolder, mGravityImages);
	
	if (kUseChaserObject)
	{
		mChaserImage = ImageFileFormat::loadFrom(File(cChaserImagePath));
		CMN_ASSERT(mChaserImage.isValid());
		mNextNewChaserObjectMS = gNowMS + 5000;
	}
	
	
	// create ship object
	{
		const CVector dummy(0, 0);
		mShipObject = this->NewObject(eShip, {dummy, dummy, dummy, 0, eIcon|eVector|eGround}, true);
		CMN_ASSERT(mShipObject);
		mShipObject->ShipReset();
		
		mShipObject->GetChild()->SetColor(Colours::red);
	}
	
	this->NewGroundObject({(double)this->GetGridWidth(), (double)this->GetGridHeight() - 50}, true);
	this->NewGroundObject({(double)this->GetGridWidth(), (double)this->GetGridHeight() - 500}, false);
}

/*---------------------------------------------------------------------------*/
void TPongView::ToggleFlatEarthObject()
{
	mFlatEarthEnabled = !mFlatEarthEnabled;
	
	if (mFlatEarthEnabled)
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
	else
	{
		if (mFlatEarthObject)
			mFlatEarthObject->SetNumHitPoints(0);
	}

}

/*---------------------------------------------------------------------------*/
void TPongView::CreateGravityObjects()
{
	mNumGravityObjects = 0;
	
	NewGravityObject({500, 600}, rndf(10,20));
	NewGravityObject({300, 400}, rndf(10,20));
	NewGravityObject({500, 200}, rndf(10,20));
	
	if (mBlackHoleEnabled)
	{
		// note: for the mass of the black hole, I suspect that we're hitting the
		// kMaxG bound in ApplyGravity so these giant masses don't change anything
		//const CVector p(rnd(1700, 2400), rnd(-750, -850));
		const CVector p(kGridWidth - 100, 60);
		CObject* blackHole = NewGravityObject(p, rndf(10000,20000));
		blackHole->SetFixed(true);
		
		String deathStar64 = kSpecialImagesFolder + "DeathStar64.png";
		mBlackHoleImage = ImageFileFormat::loadFrom(File(deathStar64));
		CMN_ASSERT(mBlackHoleImage.isValid());
		blackHole->SetImage(&mBlackHoleImage);
	}
	
	// this makes the ship part of the gravity group
	mShipObject->SetMass(20.0); // fun at 20, 100, 200, ...
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	Draw
//  tbarram 5/5/17
/*---------------------------------------------------------------------------*/
void TPongView::Draw(Graphics& g)
{
	// update the global now
	gNowMS = Time::getCurrentTime().toMilliseconds();
	
	this->CheckKeyPresses();

	// draw minimap outline
	g.drawRect(kMiniMapTopLeftCornerV.mX, kMiniMapTopLeftCornerV.mY, kMinimapWidth, kMinimapHeight, 1);
	g.drawRect(kMiniMapOuterTopLeftCorner.mX, kMiniMapOuterTopLeftCorner.mY, kMinimapOuterWidth, kMinimapOuterHeight, 1);
	
	// calc the new positions, check keypresses, etc.
	this->Animate();
	
	// draw all the objects
	mObjectPool.Draw(g);
	
	gHistoryIndex = (gHistoryIndex + 1) % kHistorySize;
	
	this->DrawText(g);
	this->DrawIntroScreens(g);
	//this->DrawDistanceMeter(g);
	this->DoDistanceGame(g);
	this->DoHostageRescueGame(g);
}

/*---------------------------------------------------------------------------*/
void TPongView::DrawDistanceMeter(Graphics& g)
{
	if (mDistanceGameStatus == eStarted &&
		mDistanceGameScore != INT_MIN)
	{
		const double kMeterLength = 400; // 300
		const double kMeterHeight = 20; // 12
		const double x = kGridWidth - kMeterLength - 480;
		const double y = 280;
		const double val = mDistanceGameScore * kMeterLength / kDistanceGameScoreStartingPoints;
		g.setColour(this->ColorForScore(mDistanceGameScore));
		g.fillRect(Rectangle<float>(x, y, val, kMeterHeight));
	}
}

/*---------------------------------------------------------------------------*/
void TPongView::CheckKeyPresses()
{
	// in case the ship gets too far off the screen
	if (this->CheckKeyPress('r', 1000))
		mShipObject->ShipReset();
	
	// M key advances to the next song
	if (this->CheckKeyPress('m', 1000) && mMusicCallback)
		mMusicCallback();
	
	// P key toggles paused state
	if (this->CheckKeyPress('p', 100))
		mIsPaused = !mIsPaused;
	
	// K key toggles ship gravity
	if (this->CheckKeyPress('k', 100))
	{
		mShipGravity = !mShipGravity;
		mShipObject->ShipReset();
	}
	
	if (this->CheckKeyPress('h', 1000))
		ObjectHistory::LogHistory();
	
	if (this->CheckKeyPress('j', 1000))
		gUsePredefinedShipPath = !gUsePredefinedShipPath;
	
	// P key toggles paused state
	if (this->CheckKeyPress('t', 700))
		this->ToggleFlatEarthObject();
	
	if (this->CheckKeyPress('k', 200))
	{
		mIntroScreen++;
		mIntroScreenChanged = true;
		const bool longIntroScreen = (mIntroScreen == eAddGravity ||
									  mIntroScreen == eGravityObjects1 ||
									  mIntroScreen == eGravityObjects2);
		mIntroScreenChangedTimeMS = gNowMS + (longIntroScreen ? 50000 : 5000);
	}
	
	// L advances level
	if (this->CheckKeyPress('l', 700))
		mKills = mNextLevelKills;
	
	// 'g' toggles the gravity objects
	if (this->CheckKeyPress('g', 1000))
	{
		// this makes the ship part of the gravity group
		mShipObject->SetMass(0); // fun at 20, 100, 200, ...
		mShipObject->ShipReset();
		mObjectPool.DestroyAllGravityObjects();
		
		if (mGravityObjectsEnabled)
			this->CreateGravityObjects();
		
		mGravityObjectsEnabled = !mGravityObjectsEnabled;
	}
}

/*---------------------------------------------------------------------------*/
void TPongView::DrawIntroText(std::string text, Graphics& g, bool start)
{
	mIntroTextCurrentY = start ? 160 : (mIntroTextCurrentY + 40);
	DrawTextAtY(text, mIntroTextCurrentY, g);
}

/*---------------------------------------------------------------------------*/
void TPongView::DrawIntroScreens(Graphics& g)
{
	g.setColour(Colours::honeydew);
	
	if (mIntroScreenChangedTimeMS &&
		mIntroScreenChangedTimeMS > gNowMS)
	{
		switch (mIntroScreen)
		{
			case eIntro:
			{
				DrawIntroText("Welcome to Space Force", g, true);
				DrawIntroText("To get started, practice flying around - L / R arrows rotate, Z thrusts", g);
				DrawIntroText("If you fly too far off the screen and need to reset, press 'R'", g);
				DrawIntroText("When you feel comfortable with the controls, press 'K' to continue", g);
				break;
			}
			case eAddGravity:
			{
				DrawIntroText("Now we're going to add gravity.", g, true);
				DrawIntroText("Press 'K' to continue", g);
				break;
			}
			case eGravityScreen:
			{
				if (mIntroScreenChanged)
				{
					mShipGravity = true;
					mShipObject->ShipReset();
				}
				DrawIntroText("Practice flying around with gravity for a while", g, true);
				DrawIntroText("Remember - if you fly too far off the screen and need to reset, press 'R'", g);
				DrawIntroText("Press 'K' to continue", g);
				break;
			}
			case eShooting1:
			{
				if (mIntroScreenChanged)
				{
					mNextNewVectorIconObjectMS = gNowMS;
					mShipObject->SetFixed(true);
				}
				DrawIntroText("Use the 'X' key or SPACE to shoot", g, true);
				DrawIntroText("Gravity (and thrust) is disabled so you can practice for a bit without moving", g);
				DrawIntroText("Press 'S' for a smart-bomb which will blow up everything on the screen", g);
				DrawIntroText("Press 'K' to continue", g);
				break;
			}
			case eShooting2:
			{
				if (mIntroScreenChanged)
					mShipObject->SetFixed(false);
				
				DrawIntroText("Gravity is back - now you have to fly and shoot", g, true);
				DrawIntroText("Use the 'X' key or SPACE to shoot", g);
				DrawIntroText("Press 'S' for a smart-bomb", g);
				DrawIntroText("Press 'K' to continue", g);
				break;
			}
			case eGravityObjects1:
			{
				if (mIntroScreenChanged)
				{
					mShipGravity = false;
					mVectorObjectActive = false;
					mNextNewVectorIconObjectMS = 0;
					this->SmartBomb();
				}
				DrawIntroText("Some objects are so massive that they attract your ship with their gravity - ", g, true);
				DrawIntroText("as well as your mass affecting them", g);
				DrawIntroText("Press 'K' to continue", g);
				break;
			}
			case eGravityObjects2:
			{
				if (mIntroScreenChanged)
				{
					this->CreateGravityObjects();
					mShipObject->EnableThrust(false);
				}
				DrawIntroText("Your thrust is temporarily disabled so you can get a feel for how the gravity objects affect you", g, true);
				DrawIntroText("As long as you don't add any external force (i.e. thrust) then you will settle into a stable orbit", g);
				DrawIntroText("Press 'K' to continue", g);
				break;
			}
			case eGravityObjects3:
			{
				if (mIntroScreenChanged)
					mShipObject->EnableThrust(true);
				
				DrawIntroText("Now your thrust is back", g, true);
				DrawIntroText("Try flying around - and try to keep the other objects from going off the screen", g);
				DrawIntroText("Press 'K' to continue", g);
				break;
			}
			case eDone1:
			{
				DrawIntroText("Have fun!", g, true);
				DrawIntroText("Press 'K' to continue", g);
				break;
			}
			case eDone2:
			{
				if (mIntroScreenChanged)
				{
					mShipGravity = true;
					mShipObject->SetMass(0);
					mObjectPool.DestroyAllGravityObjects();
					mKills = mNextLevelKills;
				}
				break;
			}
			default:
				break;
		}
		
		if (mIntroScreenChanged)
		{
			mIntroScreenChanged = false;
			mShipObject->ShipReset();
		}
	}
}

/*---------------------------------------------------------------------------*/
// DrawText
/*---------------------------------------------------------------------------*/
void TPongView::DrawText(Graphics& g)
{
	if (mOneTimeGuideExplosion)
	{
		mOneTimeGuideExplosion = false;
		this->DoExplosions();
	}
	
	if (true) // always show the stats
	{
		//const int32_t elapsedSec = (int32_t)(gNowMS - gStartTimeMS) / 1000;
		// draw text box in bottom right corner  GetNumActiveObjects
		const int32_t margin = 20;
		const CRect rect(margin, this->GetGridHeight() - margin, this->GetGridWidth() - (2 * margin), 200); // x,y,w,h
		const std::string textL =
				"\nlevel: " + std::to_string(mLevel) +
				//"\t\tkills: " + std::to_string(mKills) +
				//"\t\tdeaths: " + std::to_string(mDeaths) +
				"\t\thp: " +	std::to_string(mShipObject->GetNumHitPoints()) +
				//"\t\tbombs: " + std::to_string(mNumSmartBombs) +
				//"\t\ttime: " + std::to_string(elapsedSec) + " sec" +
				"\t\tobjects: " + std::to_string(mObjectPool.GetNumActiveObjects()) +
				"\t\tsong: " + mSongName.substr(0,32); // +
				//"\t\tgame: " + mDistanceGameString;
		
		const std::string textR =
				std::string("\n\t\t thrust: Z") +
				"\t\t  rotate: L/R arrows" +
				"\t\t shoot: X" +
				//"\t\t bomb: S" +
				"\t\t reset: R" +
				"\t\t skip song: M" +
				"\t\t continue: K";
		
		g.setColour(Colours::honeydew);
		g.drawFittedText(textL, rect, Justification::left, true);
		g.drawFittedText(textR, rect, Justification::right, true);
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
//   - do a bunch of explosions in the middle of the screen
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
	if (this->CheckKeyPress('x', 200))
		this->ShootBullet();
	
	// smart bomb
	if (KeyPress::isKeyCurrentlyDown('s') || mAutoSmartBombMode)
		this->SmartBomb();
	
	// see if we should dock
	this->CheckDockedToEarth();
	
	//if (this->CheckKeyPress('o', 700))
		//mAutoSmartBombMode = !mAutoSmartBombMode;
	
	if (this->CheckKeyPress('d', 700))
		mDistanceGameStatus = mDistanceGameStatus == eInactive ? eWaitingForStart : eInactive;
	
	if (this->CheckKeyPress(KeyPress::spaceKey, 200))
		this->SetShipSafe(2000);
}

/*---------------------------------------------------------------------------*/
void TPongView::SetShipSafe(int64_t lengthMS)
{
	mShipSafeEndMS = (gNowMS + lengthMS);
	mShipBlinkEndMS = mShipSafeEndMS;
	mShipBlinkColor = Colours::blue;
}

/*---------------------------------------------------------------------------*/
void TPongView::DrawTextAtY(std::string text, int32_t y, Graphics& g)
{
	CRect rect(0, y, this->GetGridWidth(), 20); // x,y,w,h
	g.drawText(text, rect, Justification::centred, true);
}

/*---------------------------------------------------------------------------*/
Colour TPongView::ColorForScore(int32_t score)
{
	return 	score > 3000 ? 	Colours::mediumslateblue :
			score > 1000 ? 	Colour(0xFFE1EA3C) :
							Colours::red;
}

/*---------------------------------------------------------------------------*/
void TPongView::SetDistanceGameScore(int32_t score)
{
	mDistanceGameScore = std::min(score, kDistanceGameScoreStartingPoints);
	
	// update the rotary dial UI
	if (mRotaryCallback)
		mRotaryCallback(mDistanceGameScore);
}

/*---------------------------------------------------------------------------*/
void TPongView::Rotation(int32_t numRotations)
{
	const ScoringEvent ev = 	numRotations == 1 ? eSingleRotate :
								numRotations == 2 ? eDoubleRotate :
													eTripleRotate;
	
	this->ScoreEvent(ev);
	
	if (mDistanceGameStatus != eStarted)
		return;
	
	if (numRotations >= 2)
		this->SetDistanceGameScore(kDistanceGameScoreStartingPoints);
	else
		this->SetDistanceGameScore(mDistanceGameScore + kDistanceGameRotationBonus);
}

/*---------------------------------------------------------------------------*/
std::string TPongView::TextForScoreEvent(ScoringEvent ev) const
{
	switch (ev)
	{
		case eRescuedHostage:
			return "Rescued soldier!";
		case eRescuedHostage2:
			return "Rescued spy!!";
		case eRescuedHostage3:
			return "Rescued captain!!!";
		case eSingleRotate:
			return "Nice rotate!";
		case eDoubleRotate:
			return "Nice DOUBLE rotate!!";
		case eTripleRotate:
			return "Nice TRIPLE rotate!!!";
		case eGroundCollision:
			return "Collided with ground!";
	}
}

/*---------------------------------------------------------------------------*/
int32_t TPongView::ScoreForEvent(ScoringEvent ev) const
{
	switch (ev)
	{
		case eRescuedHostage:
			return 1;
		case eRescuedHostage2:
			return 2;
		case eRescuedHostage3:
			return 3;
		case eSingleRotate:
			return 1;
		case eDoubleRotate:
			return 2;
		case eTripleRotate:
			return 3;
		case eGroundCollision:
			return -3;
	}
}

/*---------------------------------------------------------------------------*/
void TPongView::ScoreEvent(ScoringEvent ev)
{
	const int32_t score = this->ScoreForEvent(ev);
	mScore += score;
	
	std::string scoreText = ((score > 0 ? "+" : "") + std::to_string(score));
	
	if (sScoreEventHaveShownText.find(ev) == sScoreEventHaveShownText.end())
	{
		sScoreEventHaveShownText.insert({ev, true});
		scoreText = (this->TextForScoreEvent(ev) + " (" + scoreText + ")");
	}
	
	const Colour c = (score < 0 ? Colours::red : Colours::ivory);
	this->NewTextBubble(scoreText, (mShipObject->Pos() + CVector(-40,-40)), c);
}

/*---------------------------------------------------------------------------*/
void TPongView::RescuedHostage(EHostageType type)
{
	mNumHostagesSaved++;
	mShipBlinkColor = Colours::black;
	mShipBlinkEndMS = gNowMS + 1000;
	
	const ScoringEvent ev = type == eSoldier ? 	eRescuedHostage :
							type == eBoss ? 	eRescuedHostage2 :
												eRescuedHostage3;
	
	this->ScoreEvent(ev);
	
	if (mDistanceGameStatus != eStarted)
		return;
	
	//this->SetDistanceGameScore(kDistanceGameScoreStartingPoints);
}

/*---------------------------------------------------------------------------*/
void TPongView::DoHostageRescueGame(Graphics& g)
{
	StFontRestorer f(22, g);
	g.setColour(Colours::honeydew);
	
	switch (mHostageGameStatus)
	{
		case eInactive:
			break;
			
		case eWaitingForStart:
		{
			DrawTextAtY("Waiting For Start", 160, g);
			DrawTextAtY("Last Score:  " + std::to_string(mScore), 220, g);
			DrawTextAtY("Best Score:  " + std::to_string(kBestHostageGameScore), 280, g);
			
			// the game starts when the ship is close enough to start scoring
			if (mScore > 0)
			{
				// start
				mHostageGameStatus = eStarted;
				mHostageGameStartTimeMS = gNowMS;
			}
			
			break;
		}
			
		case eStarted:
		{
			DrawTextAtY("Score " + std::to_string(mScore), 120, g);
			DrawTextAtY("Hostages Saved: " + std::to_string(mNumHostagesSaved), 160, g);
			DrawTextAtY("Best score: " + std::to_string(kBestHostageGameScore), 200, g);
			
			// need to end the game
			if (/* DISABLES CODE */ (true))
			{
			}
			else
			{
				// game over - get stats and jump to eWaitingForStart
				mHostageGameStatus = eWaitingForStart;
				/*mDistanceGameDurationMS = gNowMS - mDistanceGameStartTimeMS;
				mNextDistanceGameStartTimeMS = gNowMS + kIntervalBetweenGames;*/
			}
			
			break;
		}
	}
}


/*---------------------------------------------------------------------------*/
void TPongView::DoDistanceGame(Graphics& g)
{
	// calc the distance - it's used in multiple places so we need to calc it
	// here regardless of the game state
	mShipDistanceToGround = mObjectPool.CalcShipDistanceToGround(*mShipObject);
	if (mShipDistanceToGround == INT_MAX)
		mShipDistanceToGround = 0;
	
	gSlidingAverage.AddSample(mShipDistanceToGround);
	g.setColour(Colours::honeydew);
	
	StFontRestorer f(22, g);
	
	switch (mDistanceGameStatus)
	{
		case eInactive:
			mDistanceGameString = "Inactive: " + std::to_string(mDistanceGameDurationMS);
			break;
			
		case eWaitingForStart:
		{
			mDistanceGameString = "Waiting For Start";
			mShipObject->SetFixed(false);
			
			DrawTextAtY("Waiting For Start", 160, g);
			DrawTextAtY("Last Time:  " + std::to_string((mDistanceGameDurationMS)/1000), 220, g);
			DrawTextAtY("Best Time:  " + std::to_string((mDistanceGameDurationBestMS)/1000), 280, g);
			
			// the game starts when the ship is close enough to start scoring
			if (mShipDistanceToGround > 0 &&
				mShipDistanceToGround < kDistanceGameScoreCutoff)
			{
				// start
				mDistanceGameStatus = eStarted;
				mDistanceGameStartTimeMS = gNowMS;
				gSlidingAverage.Reset();
				mDistanceGameScore = kDistanceGameScoreStartingPoints;
				this->ClearHostages();
			}
			
			break;
		}
			
		case eStarted:
		{
			mDistanceGameString = "Started";
			
			g.setColour(Colours::honeydew);
			//DrawTextAtY("Started", 100, g);
			DrawTextAtY("Time:  " + std::to_string((gNowMS - mDistanceGameStartTimeMS)/1000), 100, g);
			
			const int32_t percentage = mTotalNumHostages == 0 ? 0 : ((mNumHostagesSaved * 100)/mTotalNumHostages);
			DrawTextAtY("Hostages Saved: " + std::to_string(mNumHostagesSaved) + " out of " + std::to_string(mTotalNumHostages) +
						" (" + std::to_string(percentage) + "%)", 160, g);
			
			if (mDistanceGameScore > 0)
			{
				// game continues
				g.setColour(this->ColorForScore(mDistanceGameScore));
				//DrawTextAtY("Score:  " + std::to_string(mDistanceGameScore), 120, g);
				g.setColour(Colours::honeydew);
				
				// score goes up when you're below the cutoff, and down when above
				// might need to tune this a bit (quadratic?)
				const int32_t d = kDistanceGameScoreCutoff - mShipDistanceToGround;
				const int32_t scoreImpact = std::max(d, -kDistanceGameScoreMaxPenalty);
				this->SetDistanceGameScore(mDistanceGameScore + scoreImpact);
			}
			else
			{
				// game over - get stats and jump to eWaitingForStart
				mDistanceGameStatus = eWaitingForStart;
				mDistanceGameDurationMS = gNowMS - mDistanceGameStartTimeMS;
				mNextDistanceGameStartTimeMS = gNowMS + kIntervalBetweenGames;
				
				// explode the ship (unless we're here due to a ShipReset() call
				// in which case the ship will already have exploded)
				if (mDistanceGameScore != INT_MIN)
					this->Explosion(mShipObject->Pos(), true);
				
				this->SetDistanceGameScore(0);
				mShipObject->ShipReset();
				
				::BoundLo(mDistanceGameDurationBestMS, mDistanceGameDurationMS);
			}

			break;
		}
			
		// this state is no longer used - we now jump right into eWaitingForStart when the game ends
		case eActive:
		{
			mDistanceGameString = "Active";
			mShipObject->SetFixed(true);
			this->Explosion(mShipObject->Pos(), true);
			mShipObject->ShipReset();
			
			DrawTextAtY("Game Over", 200, g);
			DrawTextAtY("     Time:  " + std::to_string((mDistanceGameDurationMS)/1000), 260, g);
			DrawTextAtY("Best Time:  " + std::to_string((mDistanceGameDurationBestMS)/1000), 320, g);
			
			// see if it's time to start the next game
			if (mNextDistanceGameStartTimeMS)
			{
				const int64_t nextStartMS = mNextDistanceGameStartTimeMS - gNowMS;
				if (nextStartMS > 0)
					DrawTextAtY("next game starts in:  " + std::to_string(nextStartMS/1000), 380, g);
				else
					mDistanceGameStatus = eWaitingForStart;
			}
			break;
		}
	}
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
		if (d < 50 && fabs(mShipObject->GetAngle()) < M_PI_4 &&
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
	
	// update level based on kills
	if (mKills >= mNextLevelKills)
	{
		mLevel++;
		mNumSmartBombs++; // extra smart bomb with each new level
		
		// start the level pause screen
		//if (mLevel > 1)
		{
			this->SmartBomb();
			mShowLevelTextUntilMS = gNowMS + 3000;
		}
		
		int32_t killsToAdvance = 10;
		switch (mLevel)
		{
			case 1:
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
void TPongView::CreateNewObjects()
{
	// create a new falling icon object if it's time, and schedule the next one
	if (CheckDeadline(mNextNewFallingIconObjectMS))
	{
		this->NewFallingIconObject();
		
		// schedule the next one - they get faster as more kills accumulate
		const int32_t kMultiplier = (mLevel - 1);
		const int32_t fixedMS = std::max(500 - (kMultiplier * 100), 100);
		const int32_t randomMaxMS = std::max(1200 - (kMultiplier * 100), 500);
		const int32_t nextMS = (fixedMS + (rnd(randomMaxMS)));
		mNextNewFallingIconObjectMS = (gNowMS + nextMS);
	}
	
	// create a new crawling icon object if it's time, and schedule the next one
	if (CheckDeadline(mNextNewCrawlingIconObjectMS))
	{
		this->NewCrawlingIconObject();
		mNextNewCrawlingIconObjectMS = (gNowMS + 3000);
	}
	
	// create a new vector icon object if it's time, and schedule the next one
	if (CheckDeadline(mNextNewVectorIconObjectMS) && !mVectorObjectActive)
	{
		this->NewVectorIconObject();
	}
	
	if (CheckDeadline(mNextNewChaserObjectMS))
	{
		mNextNewChaserObjectMS = 0;
		this->NewChaserObject();
	}
}

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
void TPongView::NewTextBubble(std::string text, CVector pos, Colour color)
{
	const CVector v(-20, -50);
	const CVector a(-20, -20);
	const int64_t lifetime = 3000;
	CObject* obj = this->NewObject(eTextBubble, {pos, v, a, lifetime, 0});
	obj->SetTextBubbleTest(text);
	obj->SetColor(color);
}

/*---------------------------------------------------------------------------*/
CObject* TPongView::NewGravityObject(CVector pos, double mass)
{
	static const int32_t killedBy = eBullet;
	CObject* obj = this->NewObject(eGravity, {pos, zero, zero, 0, killedBy}, true);
	obj->SetMass(mass);
	
	CMN_ASSERT(mGravityImages.size() > 0);
	obj->SetImage(&mGravityImages[mNumGravityObjects]);
	mNumGravityObjects = (mNumGravityObjects + 1) % mGravityImages.size();
	
	return obj;
}

/*---------------------------------------------------------------------------*/
void TPongView::NewGroundObject(CVector pos, bool isBottom)
{
	const CVector v(isBottom ? -kGroundSpeed : (-kGroundSpeed - 20), 0);
	CObject* groundObject = this->NewObject(eGround, {pos, v, zero, 0, 0});
	this->GetObjectPool().AddGroundObject(groundObject);
	groundObject->InitGround(isBottom);
	
	if (isBottom && CheckDeadline(mNextHostageObjectMS))
	{
		mNextHostageObjectMS = 0;
		// create hostage object attached to this ground object
		CObject* hostage = this->NewObject(eHostage, {zero, zero, zero, 0, eShip});
		hostage->SetGroundObjectForHostage(groundObject);
		
		const int32_t rand = rnd(10);
		const EHostageType type = (rand < 5 ? eSoldier : rand < 8 ? eBoss : eFriend);
		hostage->SetHostageType(type);
		hostage->SetImage(&this->GetHostageImage(type));
		
		const CVector offset(-8, -16 - rnd(12));
		hostage->SetHostageOffset(offset);
		mNextHostageObjectMS = (gNowMS + rnd(2000, 6000));
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	NewFallingIconObject
//  tbarram 4/30/17
/*---------------------------------------------------------------------------*/
void TPongView::NewFallingIconObject()
{
	// create a new object with random horiz start location and gravity -
	// they have 0 velocity, only (random) vertical acceleration
	static const int32_t horizMaxStart = (this->GetGridWidth() - 10);
	const CVector p(rnd(horizMaxStart), 0);
	const CVector a(0, 5 + rnd(100));
	static const int32_t killedBy = eBullet | eShip | eGround;
	this->NewObject(eIcon, {p, zero, a, 0, killedBy});
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
	static const int32_t killedBy = eBullet | eShip | eGround;
	this->NewObject(eIcon, {p, v, a, 0, killedBy});
}

/*---------------------------------------------------------------------------*/
void TPongView::NewChaserObject()
{
	static const CVector p(this->GetGridWidth()/2, 200);
	static const int32_t killedBy = eBullet;
	this->NewObject(eChaser, {p, zero, zero, 0, killedBy});
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
	
	static const int32_t killedBy = eBullet | eShip;
	CObject* obj = this->NewObject(eVector, {zero, zero, zero, 0, killedBy});
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
/*---------------------------------------------------------------------------*/
void TPongView::ShootBullet()
{
	// start the bullet at the front of the ship in the ship's direction
	static const double speed = 1600.0; // 400.0
	const CVector v = CVector::Velocity(speed, 0, {mShipObject->GetSin(), mShipObject->GetCos()});
	const int64_t lifetime = 5000;
	this->NewObject(eBullet, {mShipObject->GetFront(), v, zero, lifetime, eIcon | eVector});
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	SmartBomb
/*---------------------------------------------------------------------------*/
void TPongView::SmartBomb()
{
	mObjectPool.KillAllObjectsOfType(eIcon | eVector);
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	VectorObjectDied
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
	mNextNewChaserObjectMS = (gNowMS + 10000);
}

/*---------------------------------------------------------------------------*/
void TPongView::HostageObjectDied()
{
	mTotalNumHostages++;
}

/*---------------------------------------------------------------------------*/
CVector& TPongView::GetChaserPosition()
{
	// TODO: make chaserDistance get bigger and smaller
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
	
	const double d = ::Distance(obj1.Pos(), obj2.Pos());
	const double g = (G * obj1.GetMass() * obj2.GetMass()) / (d * d);
	
	// bound the gravity to a specific range
	double g_adjusted = g + kMinG;
	::Bound(g_adjusted, kMinG, kMaxG);
	
	const double angleRad = CObject::ArcTan2(obj1, obj2);
	
	// create the acceleration vectors in both directions
	CVector a(g_adjusted * ::sin(angleRad), g_adjusted * ::cos(angleRad));
	CVector a_neg(-a.mX, -a.mY);
	
	// apply them
	obj1.IncrementAcc(a_neg);
	obj2.IncrementAcc(a);
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	ShipReset
//  tbarram 5/8/17
/*---------------------------------------------------------------------------*/
void CObject::ShipReset()
{
	//printf("ShipReset kHostageRescueGameLifeCounter = %d \n", kHostageRescueGameLifeCounter);
	
	mState.mPos = {float(mPongView->GetGridWidth()/2), float(mPongView->GetGridHeight() - kGroundMidpoint)};
	
	//mState.mVel = {0, (double)(mPongView->ShipGravity() ? -90 : 0)}; // start with upward thrust since gravity will quickly kick in
	mState.mVel = {0, (double)(mPongView->ShipGravity() ? 0 : 0)};
	mState.mAcc = {0, (double)(mPongView->ShipGravity() ? 80 : 0)};
	mAngle = 0.0;
	this->SetReadyAfter(gNowMS + 100); // hide ship for a few seconds when it gets destroyed
	this->SetNumHitPoints(6); // reset
	mVertices.clear();
	mDockedToEarthMS = 0;
	gSlidingAverage.Reset();
	mPongView->SetDistanceGameScore(INT_MIN);
	mPongView->SetShipSafe(3000);
	
	mWasRotating = false;
	
	// need to change this so it only happens once the game has started
	if (mHostageGameStatus == eStarted)
		mPongView->ScoreEvent(eGroundCollision);
	
	// see if we should reset the hostage resuce counter
	bool resetHostageCount = false;
	if (kDoDistanceGame)
	{
		// reset every time you die for the distance game
		resetHostageCount = true;
	}
	else if (kDoHostageRescueGame)
	{
		// reset every time you run out of lives
		if (++kHostageRescueGameLifeCounter == kHostageRescueGameNumLives)
		{
			kHostageRescueGameLifeCounter = 0;
			resetHostageCount = true;
			
			if (kBestHostageGameScore < mScore)
				kBestHostageGameScore = mScore;
		}
	}
	
	if (resetHostageCount)
		mPongView->ClearHostages();
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	IsAlive
//   - when an object returns false, it will be removed from the list and deleted
/*---------------------------------------------------------------------------*/
bool CObject::IsAlive() const
{
	if (this->GetParent())
		return this->GetParent()->IsAlive();
	
	if (this->Is(eShip))
		return true;
	
	if (this->IsDestroyed())
		return false;
	
	// the ground objects (each line segment) die when they go off the screen
	if (this->Is(eGround))
		return mRightEndpoint.mX > 0;
	
	if (CheckDeadline(mState.mExpireTimeMS))
		return false;
	
	// objects that leave the bottom edge never come back
	if (!this->Is(eGravity) && mState.mPos.mY >= kGridHeight)
		return false;
	
	// most objects die when they disappear
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
	
	if (this->Is(eHostage))
		mPongView->HostageObjectDied();
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	Explode
//   - send a bunch of fragments off from the passed-in position
/*---------------------------------------------------------------------------*/
void TPongView::Explosion(const CVector& pos, bool isShip)
{
	const int32_t kNumFrags = isShip ? 22 : rnd(6, 12);
	const double kAngleInc = 2 * M_PI / kNumFrags;
	
	for (int32_t j = 0; j < kNumFrags; j++)
	{
		// give each frag a random speed
		const double speed = rnd(60, 180);
		
		// send each of the fragments at an angle equally spaced around the unit
		// circle, with some randomness
		const double rndMin = -M_PI/(isShip ? 4 : 8);
		const double rndMax = M_PI/(isShip ? 4 : 8);
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
		mState.mVel = f->Vel(); // probably not needed
		mState.mAcc = f->Acc(); // probably not needed
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
	}

	// apply velocity to position
	//mState.mPos += (mState.mVel * diffSec);
	mState.mPos.mX += (mState.mVel.mX * diffSec);
	mState.mPos.mY += (mState.mVel.mY * diffSec);
	
	if (this->Is(eHostage))
	{
		// the hostage's position is anchored to the ground object it was created on
		if (mGroundObjectForHostage)
		{
			const CVector shake(0, rnd(3));
			mState.mPos = (mGroundObjectForHostage->Pos() + mHostageOffset + shake);
		}
	}
	
	// mCollisionRect is for calculating collisions with other objects (not collisions with
	// the ground though - mVertices is used for that for better resolution)
	mCollisionRect = CRect(mState.mPos.mX, mState.mPos.mY, mWidth, mHeight);
	
	if (this->Is(eBullet))
	{
		// calc previous position rects for bullet collisions - we need these so
		// the bullets don't pass through objects due to low frame rate -
		// i.e. CCD (Continuous Collision Detection)
		const double diffSecInc = diffSec / (double)kNumBulletCollisionRects;
		for (int32_t k = 0; k < kNumBulletCollisionRects; k++)
		{
			const double inc = diffSecInc * k;
			const double v = mState.mPos.mY + (mState.mVel.mY * inc);
			const double h = mState.mPos.mX + (mState.mVel.mX * inc);
			mBulletCollisionRects[k] = CRect(h, v, h + mWidth, v + mHeight);
		}
	}
	
	if (this->WrapsHorizontally())
	{
		static const int32_t gridWidth = mPongView->GetGridWidth();
		if (mState.mPos.mX > gridWidth)
			mState.mPos.mX = 0;
		else if (mState.mPos.mX < 0)
			mState.mPos.mX = gridWidth;
	}
		
	if (this->Is(eShip))
	{
		if (this->IsOnGround())
		{
			// when ship hits ground, set vertical velocity & accel back to 0
			mState.mVel.mY = 0;
			if (!mPongView->ShipGravity())
				mState.mAcc.mY = 0;
		}
		
		if (mPongView->DistanceGameActive())
		{
			if (mState.mPos.mX < 0 || mState.mPos.mX > kGridWidth)
			{
				mPongView->Explosion(mState.mPos, this->Is(eShip));
				this->ShipReset();
			}
		}
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	CollidedWith
//  tbarram 1/26/18
/*---------------------------------------------------------------------------*/
bool CObject::CollidedWith(CObject& other)
{
	// they shouldn't both be bullets
	CMN_ASSERT(!(this->Is(eBullet) && other.Is(eBullet)));
	
	// check if one is a bullet
	if (this->Is(eBullet) || other.Is(eBullet))
	{
		CObject* bullet = this->Is(eBullet) ? this : &other;
		CObject* nonBullet = this->Is(eBullet) ? &other : this;
		
		for (int32_t k = 0; k < kNumBulletCollisionRects; k++)
			if (this->CollidedWith(bullet->mBulletCollisionRects[k], nonBullet->mCollisionRect))
				return true;
		
		return false;
	}
	else
	{
		// neither are bullets
		return this->CollidedWith(this->mCollisionRect, other.mCollisionRect);
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	CollidedWith
/*---------------------------------------------------------------------------*/
bool CObject::CollidedWith(CRect& a, CRect& b)
{
	return a.intersects(b);
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	DistanceToLine
/*---------------------------------------------------------------------------*/
int32_t CObject::DistanceToLine(CVector right, CVector left, CVector pt)
{
	// only check the segment we're in
	if (pt.mX < left.mX || pt.mX > right.mX)
		return INT_MAX;
	
	// avoid divide by zero
	if (right.mX - left.mX == 0)
		return INT_MAX;
	
	// slope
	const double m = (right.mY - left.mY) / (right.mX - left.mX);

	// b = y - mx, since y = mx + b
	const double b = right.mY - (m * right.mX);
	
	// now that we have the equation of the line, find the y value of the
	// point on the line with the x-coord of the ship (y = mx + b)
	const double y = (m * pt.mX) + b;
	
	// the distance is the vertical line from the ship to the line segment
	const double d = y - pt.mY;
	return d;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	CalcDistanceToGround
/*---------------------------------------------------------------------------*/
int32_t CObject::CalcDistanceToGround(CObject& ground, CObject& obj)
{
	const std::vector<CPointI>& vertices = obj.GetVertices();
	
	int32_t distance = INT_MAX;
	for (const auto& v : vertices)
	{
		const int32_t d = DistanceToLine(ground.mRightEndpoint, ground.mLeftEndpoint, CVector(v.x, v.y));
		if (d < distance)
			distance = d;
	}
	
	return distance;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	IsUnderLine
/*---------------------------------------------------------------------------*/
bool CObject::IsUnderLine(CVector right, CVector left, CVector pt)
{
	static const int32_t kGroundTolerance = 0;
	return DistanceToLine(right, left, pt) < -kGroundTolerance;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	IsAboveLine
/*---------------------------------------------------------------------------*/
bool CObject::IsAboveLine(CVector right, CVector left, CVector pt)
{
	const int32_t d = DistanceToLine(right, left, pt);
	if (d > 1000)
		return false;
	else
		return d > 0;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	CollidedWithGround
/*---------------------------------------------------------------------------*/
bool CObject::CollidedWithGround(CObject& ground, CObject& obj)
{
	if (mShipSafeEndMS)
		return false;
	
	const bool isBottom = ground.IsBottom();
	const std::vector<CPointI>& vertices = obj.GetVertices();
	for (const auto& v : vertices)
	{
		bool outOfBounds = false;
		if (isBottom)
			outOfBounds = IsUnderLine(ground.mRightEndpoint, ground.mLeftEndpoint, CVector(v.x, v.y));
		else
			outOfBounds = IsAboveLine(ground.mRightEndpoint, ground.mLeftEndpoint, CVector(v.x, v.y));
		
		if (outOfBounds)
			return true;
	}
		
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
	
	if (this->Is(eHostage))
		mPongView->RescuedHostage(mHostageType);
	
	if (type == eSmart || type == eWithGround || this->Is(eChaser))
		mHitPoints = 1;
	
	if (mHitPoints > 0 && --mHitPoints == 0)
	{
		if (!mPongView->LevelPause())
		{
			// blow us up
			if (!this->Is(eHostage))
				mPongView->Explosion(mState.mPos, this->Is(eShip));
		
			// keep stats
			if (this->Is(eIcon) || this->Is(eVector))
				mPongView->AddKill();
			
			else if (this->Is(eShip))
			{
				mPongView->AddDeath();
				this->ShipReset();
			}
		}
	}
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	Rotate
//  - rotate point p around a center point c based on the ship's angle
/*---------------------------------------------------------------------------*/
void CObject::Rotate(CPointF& p, const CPointF& c)
{
	// normalize
	p.x -= c.x;
	p.y -= c.y;

	// rotate
	const double h = (p.x * mAngleCos - p.y * mAngleSin);
	const double v = (p.x * mAngleSin + p.y * mAngleCos);

	// un-normalize
	p.x = (h + c.x);
	p.y = (v + c.y);
}

/*---------------------------------------------------------------------------*/
void CObject::GetPredefinedShipData()
{
	ObjectHistory& h = ObjectHistory::gPredefinedShipPath[gShipPathIndex];
	gShipPathIndex = ((gShipPathIndex + 1) % ObjectHistory::gPredefinedShipPath.size());
	mState.mPos.mX = h.mX;
	mState.mPos.mY = h.mY;
	mAngle = h.mAngle;
	mAngleSin = ::sin(mAngle);
	mAngleCos = ::cos(mAngle);
	mThrusting = (h.mThrusting == 1);
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	AnimateShip
/*---------------------------------------------------------------------------*/
void CObject::AnimateShip()
{
	if (gUsePredefinedShipPath && this->Is(eShip))
		this->GetPredefinedShipData();
	
	if (kFreezeShipInMiddle)
		mState.mPos = {600, 400};
	
	// position refers to the center of the triangle
	CPointF pos(mState.mPos.mX, mState.mPos.mY);
	
	static const int32_t kBaseWidth = 16;
	static const int32_t kHeight = 8; //24;
	static const int32_t kHalfBaseWidth = kBaseWidth / 2;
	static const int32_t kHalfHeight = kHeight / 2;
	static const int32_t kCenterIndent = 4;
	static const int32_t kThrustWidth = (kBaseWidth / 4) - 1;
	static const int32_t kThrustHeight = 8;
	
	// the ship has 4 vertices
	std::list<CPointF> ship;
	ship.push_back(CPointF(pos.x - kHalfBaseWidth, pos.y + kHalfHeight)); // bottomL
	ship.push_back(CPointF(pos.x, pos.y + kHalfHeight - kCenterIndent)); // bottomC
	ship.push_back(CPointF(pos.x + kHalfBaseWidth, pos.y + kHalfHeight)); // bottomR
	ship.push_back(CPointF(pos.x, pos.y - kHalfHeight));	// top
	
	// mVertices is used for drawing and for collision-with-ground detection
	mVertices.clear();
	mVertices.reserve(ship.size());
	
	// rotate each point and add to mVertices
	for (auto& pt : ship)
	{
		this->Rotate(pt, pos);
		mVertices.push_back({(int32_t)pt.x, (int32_t)pt.y});
	}
	
	mCollisionRect = CRect::findAreaContainingPoints((CPointI*)(&mVertices[0]), (int32_t)mVertices.size());
	
	// cache mFront for bullet origin
	mFront = CPointF(pos.x, pos.y - kHalfHeight);
	this->Rotate(mFront, pos);

	mThrustVertices.clear();
	if (mThrusting)
	{
		std::list<CPointF> thrust;
		thrust.push_back(CPointF(pos.x - kThrustWidth, pos.y + kHalfHeight)); // bottomL
		thrust.push_back(CPointF(pos.x, pos.y + kHalfHeight + kThrustHeight)); // bottomC
		thrust.push_back(CPointF(pos.x + kThrustWidth, pos.y + kHalfHeight)); // bottomR
		
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
/*---------------------------------------------------------------------------*/
void CObject::DrawShip(Graphics& g)
{
	if (mDistanceFromGround < kDistanceGameScoreCutoff)
		g.setColour(Colours::mediumslateblue);
	else
		g.setColour(Colours::lawngreen);
	
	if (CheckDeadline(mShipSafeEndMS))
		mShipSafeEndMS = 0;
	
	if (mShipBlinkEndMS)
	{
		const int64_t timeLeft = (mShipBlinkEndMS - gNowMS);
		const bool useColor = (timeLeft / 100) % 2;
		if (timeLeft > 0)
			g.setColour(useColor ? mShipBlinkColor : Colours::white);
		else
			mShipBlinkEndMS = 0;
	}
	
	auto& v = mVertices;
	auto& tv = mThrustVertices;
	
	// draw ship
	{
		Path p;
		p.startNewSubPath(CPointF(v[0].x, v[0].y));
		for (int k = 1; k <= 3; k++)
			p.lineTo(CPointF(v[k].x, v[k].y));
		p.closeSubPath();
		g.fillPath(p);
	}
	
	// draw thrust
	if (tv.size() > 0 && tv[0].x != 0 && tv[0].x != -1)
	{
		g.setColour(Colours::red);
		Path p;
		p.addTriangle(tv[0].x, tv[0].y, tv[1].x, tv[1].y, tv[2].x, tv[2].y);
		g.fillPath(p);
	}
}

/*---------------------------------------------------------------------------*/
void CObject::CheckRotation(bool isRotating)
{
	if (isRotating)
	{
		if (!mWasRotating)
		{
			// the ship just started rotating - snapshot the start angle
			mAngleStart = mAngle;
		}
		else
		{
			// the ship is continuing its rotation - calc the angular change (in radians)
			const double angularChange = ::fabs(mAngleStart - mAngle);
			
			// calc the threshold for the next rotation
			// it's pi/4 less than a full rotation to make it a little easier
			const double nextRotationThreshold = (((mNumRotations + 1) * 2 * M_PI) - M_PI_4);
			
			// see if we have crossed the threshold
			if (angularChange > nextRotationThreshold)
			{
				mNumRotations++;
				mShipBlinkColor = (mNumRotations > 1 ? Colours::blue : Colours::red);
				mShipBlinkEndMS = (gNowMS + (mNumRotations > 1 ? 1600 : 800));
				mPongView->Rotation(mNumRotations);
			}
		}
	}
	else
	{
		mNumRotations = 0;
	}
	
	// what was is what is
	mWasRotating = isRotating;
}

/*---------------------------------------------------------------------------*/
// 	METHOD:	GetControlData
/*---------------------------------------------------------------------------*/
void CObject::GetControlData()
{
	bool isRotating = false;
	
	if (KeyPress::isKeyCurrentlyDown(KeyPress::rightKey) ||
		KeyPress::isKeyCurrentlyDown('d'))
	{
		mAngle += kRotateSpeed;
		isRotating = true;
	}
	
	if (KeyPress::isKeyCurrentlyDown(KeyPress::leftKey) ||
		KeyPress::isKeyCurrentlyDown('a'))
	{
		mAngle -= kRotateSpeed;
		isRotating = true;
	}
	
	this->CheckRotation(isRotating);
	
	// disable thrust after the ship has been rotating for a bit - this
	// way you can't just keep rotating by thrusting - might need to
	// tune this so we don't ruin the ability to make tight turns
	const bool hasBeenRotatingABit =
		(mWasRotating && ::fabs(mAngleStart - mAngle) > M_PI_2);
	
	// clamp at 0 when it gets close so ship gets truly flat
	if (::fabs(mAngle) < 0.0001)
		mAngle = 0.0;
	
	// calc and cache sin & cos
	mAngleSin = ::sin(mAngle);
	mAngleCos = ::cos(mAngle);
	
	const bool onlyVerticalThrust = false;
	
	mThrusting = false;
	if (mThrustEnabled && !hasBeenRotatingABit &&
		(KeyPress::isKeyCurrentlyDown('z') ||
		 KeyPress::isKeyCurrentlyDown('w') ||
		 KeyPress::isKeyCurrentlyDown(KeyPress::upKey)))
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
			const double cos = onlyVerticalThrust ? ::cos(0) : mAngleCos;
			const double sin = onlyVerticalThrust ? ::sin(0) : mAngleSin;
			mState.mVel.mY -= (cos * kThrustSpeed); // vertical thrust
			mState.mVel.mX += (sin * kThrustSpeed); // horiz thrust
			mThrusting = true;
		}
	}
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
/*---------------------------------------------------------------------------*/
void CObject::DrawGroundObject(Graphics& g)
{
	g.setColour(Colours::lawngreen);
	
	// the position of the line segment is defined as its left endpoint
	mLeftEndpoint = mState.mPos;
	mRightEndpoint = {mLeftEndpoint.mX + mWidth, mLeftEndpoint.mY + mHeight};
	
	this->LineBetween(g, mLeftEndpoint, mRightEndpoint);
	
	// draw the ground in the minimap
	//CVector miniMapL = TranslateForMinimap(mLeftEndpoint);
	//CVector miniMapR = TranslateForMinimap(mRightEndpoint);
	//this->LineBetween(g, miniMapL, miniMapR, 1);
	
	// when this line segment's right side hits the right edge, create the next one
	if (!mHasTriggeredNext && mRightEndpoint.mX <= mPongView->GetGridWidth())
	{
		mHasTriggeredNext = true;
		
		// start the next object - the right endpoint of the current object is
		// the left endpoint of the new one
		mPongView->NewGroundObject(mRightEndpoint, mIsBottom);
	}
}

/*---------------------------------------------------------------------------*/
void CObject::DrawTextBubble(Graphics& g)
{
	StFontRestorer r({"helvetica", 18, 0}, g);
	g.setColour(mColor);
	CRect rect(mState.mPos.mX, mState.mPos.mY, mWidth, mHeight); // x,y,w,h
	g.drawText(mTextBubbleText, rect, Justification::left, true);
}

/*---------------------------------------------------------------------------*/
void CObject::SetImage(Image* img)
{
	CMN_ASSERT(img->isValid());
	mImage = img;
	mWidth = mImage->getWidth();
	mHeight = mImage->getHeight();
}

/*---------------------------------------------------------------------------*/
// not used anymore since we now use an image
void CObject::DrawBullet(Graphics& g)
{
	const int32_t segmentW = 2;
	const int32_t length = 14;
	const int32_t outerSegmentLength = 10;
	const int32_t diff = (length - outerSegmentLength) / 2;
	
	g.setColour(Colours::green);
	const double rads = ::atan2(mState.mVel.mX, -mState.mVel.mY);
	Line<float> bullet = Line<float>::fromStartAndAngle({(float)mState.mPos.mX, (float)mState.mPos.mY}, length, rads);
	g.drawLine(bullet, segmentW);
	
	CPointF p = bullet.getPointAlongLine(diff, segmentW);
	Line<float> bullet2 = Line<float>::fromStartAndAngle(p, outerSegmentLength, rads);
	g.setColour(Colours::yellow);
	g.drawLine(bullet2, segmentW);
	
	p = bullet.getPointAlongLine(diff, -segmentW);
	Line<float> bullet3 = Line<float>::fromStartAndAngle(p, outerSegmentLength, rads);
	g.setColour(Colours::red);
	g.drawLine(bullet3, segmentW);
}

/*---------------------------------------------------------------------------*/
void CObject::InitGround(bool isBottom)
{
	mIsBottom = isBottom;
	
	// how close are the tight corridors
	const int32_t kMinCloseness = 32;
	const int32_t kMaxDiff = 320;
	
	// as this increase, the ground gets narrower
	int32_t dec = mHostageGameStartTimeMS ? ((int32_t)(gNowMS - mHostageGameStartTimeMS) / 10000) : 0;
	if (dec > 20)
		dec = 20;
	
	const int32_t minCloseness = (kMinCloseness - dec);
	const int32_t maxDiff = (kMaxDiff - (dec * 8));
	
	const int32_t kUpperLineMin = kGroundMidpoint + (maxDiff/2);
	const int32_t kUpperLineMax = kGroundMidpoint + (minCloseness/2);
	const int32_t kLowerLineMin = kGroundMidpoint - (minCloseness/2);
	const int32_t kLowerLineMax = kGroundMidpoint - (maxDiff/2);
	
	// each ground object is a new line segment
	// get random values for the width and height of this line segment
	mWidth = rnd(30, 120);
	int32_t height = rnd(10, 100);
	
	// make sure the line segments stay within the above ^^ range
	if (mIsBottom)
	{
		mRangeMinY = (mPongView->GetGridHeight() - kLowerLineMin);
		mRangeMaxY = (mPongView->GetGridHeight() - kLowerLineMax);
	}
	else
	{
		mRangeMinY = (mPongView->GetGridHeight() - kUpperLineMin);
		mRangeMaxY = (mPongView->GetGridHeight() - kUpperLineMax);
	}
	
	bool& increasingSlope = mIsBottom ? sIncreasingSlopeBottom : sIncreasingSlopeTop;
	
	if (increasingSlope && (height > (mState.mPos.mY - mRangeMinY)))
		height = mState.mPos.mY - mRangeMinY;
	else if (!increasingSlope && (height > (mRangeMaxY - mState.mPos.mY)))
		height = mRangeMaxY - mState.mPos.mY;
	
	// switch increasing & decreasing
	mHeight = height * (increasingSlope ? -1.0 : 1.0);
	increasingSlope = !increasingSlope;
	
	// set this non-zero so the object doesn't immediately die in IsAlive()
	mRightEndpoint.mX = 1;
	return;
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
		case eFlatEarth:
		{
			mImage = &mPongView->GetFlatEarthImage();
			usesImage = true;
			break;
		}
		/*case eBullet: // bullets are red
		{
			mColor = Colours::red;
			//const int32_t size = mPongView->GetShipObject()->IsDockedToEarth() ? 6 :4;
			mWidth = 4;
			mHeight = 10;
			break;
		}*/
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
			mWidth = mHeight = 4;
			break;
		}
		case eBullet:
		{
			mImage = &mPongView->GetBulletImage();
			usesImage = true;
			break;
		}
		case eTextBubble:
		{
			mHeight = 20;
			mWidth = 300;
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
		this->DrawShip(g);
	}
	else if (this->Is(eGround))
	{
		this->DrawGroundObject(g);
	}
	else if (this->Is(eTextBubble))
	{
		this->DrawTextBubble(g);
	}
	else if (mImage && mImage->isValid())
	{
		// not sure why we can't just use mCollisionRect here in drawImage
		const Rectangle<float> r(mState.mPos.mX, mState.mPos.mY, mWidth, mHeight);
		g.setOpacity(1.0f);
		g.drawImage(*mImage, r, RectanglePlacement::centred);
	}
	else
	{
		g.setColour(mColor);
		g.fillEllipse(mState.mPos.mX, mState.mPos.mY, mWidth, mHeight);
	}

	if (!this->Is(eShip))
	{
		// for checking collision with ground - just use the center point for non-ship objects
		// (for the ship we use 4 vertices and check each for ground collision)
		mVertices.push_back({(int32_t)mState.mPos.mX, (int32_t)mState.mPos.mY});
	}
	
	// add the history snapshot
	if (this->Is(eShip))
	{
		ObjectHistory& h = ObjectHistory::gShipHistory[gHistoryIndex];
		h.AddSample(mState.mPos.mX, mState.mPos.mY, mAngle, mThrusting);
	}
	
	if (kDrawCollisionRectOutline && !this->Is(eGround))
	{
		g.setColour(Colours::yellow);
		g.drawRect(mCollisionRect);
	}
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
		
		if (nextPos.mY == mState.mPos.mY &&
			nextPos.mX == mState.mPos.mX)
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
			
			mState.mVel.mX = (speed * ::cos(angle));
			mState.mVel.mY = (speed * ::sin(angle));
		}
	}
}

/*---------------------------------------------------------------------------*/
// TODO: move this into a different file
std::vector<ObjectHistory> ObjectHistory::gPredefinedShipPath = {
	ObjectHistory(146,407,1.25663698,1),
	ObjectHistory(156,406,1.25663698,1),
	ObjectHistory(168,405,1.25663698,1),
	ObjectHistory(180,404,1.25663698,1),
	ObjectHistory(193,403,1.25663698,1),
	ObjectHistory(205,402,1.25663698,0),
	ObjectHistory(218,401,1.25663698,0),
	ObjectHistory(230,400,1.25663698,0),
	ObjectHistory(244,399,1.25663698,0),
	ObjectHistory(256,399,1.25663698,0),
	ObjectHistory(269,398,1.25663698,0),
	ObjectHistory(282,397,1.25663698,0),
	ObjectHistory(294,397,1.25663698,0),
	ObjectHistory(307,397,1.25663698,0),
	ObjectHistory(319,396,1.09955740,0),
	ObjectHistory(332,396,0.94247776,0),
	ObjectHistory(345,396,0.78539813,0),
	ObjectHistory(357,396,0.62831849,0),
	ObjectHistory(371,396,0.47123885,0),
	ObjectHistory(383,396,0.31415921,0),
	ObjectHistory(396,397,0.15707958,0),
	ObjectHistory(408,397,0.00000000,0),
	ObjectHistory(421,397,-0.15707964,0),
	ObjectHistory(433,398,-0.31415927,0),
	ObjectHistory(447,399,-0.47123891,0),
	ObjectHistory(459,399,-0.62831855,0),
	ObjectHistory(471,400,-0.78539819,0),
	ObjectHistory(484,401,-0.94247782,0),
	ObjectHistory(497,402,-1.09955740,0),
	ObjectHistory(509,403,-1.25663698,0),
	ObjectHistory(522,404,-1.41371655,0),
	ObjectHistory(535,406,-1.57079613,0),
	ObjectHistory(548,407,-1.72787571,0),
	ObjectHistory(560,408,-1.88495529,0),
	ObjectHistory(573,410,-2.04203486,0),
	ObjectHistory(586,411,-2.19911456,0),
	ObjectHistory(598,413,-2.35619426,0),
	ObjectHistory(611,415,-2.51327395,0),
	ObjectHistory(624,417,-2.67035365,0),
	ObjectHistory(637,419,-2.82743335,0),
	ObjectHistory(649,421,-2.98451304,0),
	ObjectHistory(662,423,-3.14159274,0),
	ObjectHistory(675,425,-3.29867244,0),
	ObjectHistory(687,427,-3.45575213,0),
	ObjectHistory(700,430,-3.61283183,0),
	ObjectHistory(713,432,-3.76991153,0),
	ObjectHistory(726,435,-3.92699122,0),
	ObjectHistory(738,438,-4.08407068,0),
	ObjectHistory(751,440,-4.24115038,0),
	ObjectHistory(763,443,-4.39823008,0),
	ObjectHistory(777,446,-4.55530977,0),
	ObjectHistory(789,449,-4.71238947,0),
	ObjectHistory(801,452,-4.86946917,0),
	ObjectHistory(814,456,-5.02654886,0),
	ObjectHistory(827,459,-5.18362856,0),
	ObjectHistory(839,462,-5.34070826,0),
	ObjectHistory(852,465,-5.49778795,1),
	ObjectHistory(866,468,-5.65486765,1),
	ObjectHistory(881,470,-5.81194735,1),
	ObjectHistory(894,471,-5.96902704,1),
	ObjectHistory(908,472,-6.12610674,1),
	ObjectHistory(923,473,0.00000000,1),
	ObjectHistory(937,472,-0.15707964,1),
	ObjectHistory(951,472,-0.31415927,1),
	ObjectHistory(964,470,-0.47123891,1),
	ObjectHistory(977,469,-0.62831855,1),
	ObjectHistory(990,467,-0.78539819,1),
	ObjectHistory(1002,464,-0.94247782,1),
	ObjectHistory(1013,462,-0.94247782,1),
	ObjectHistory(1024,459,-0.94247782,1),
	ObjectHistory(1035,455,-0.94247782,1),
	ObjectHistory(1045,452,-0.94247782,1),
	ObjectHistory(1054,448,-0.94247782,1),
	ObjectHistory(1063,443,-0.94247782,1),
	ObjectHistory(1072,439,-0.94247782,1),
	ObjectHistory(1080,434,-0.94247782,1),
	ObjectHistory(1087,429,-0.94247782,1),
	ObjectHistory(1093,424,-0.94247782,1),
	ObjectHistory(1100,418,-0.94247782,1),
	ObjectHistory(1105,412,-0.94247782,1),
	ObjectHistory(1110,406,-0.94247782,1),
	ObjectHistory(1115,399,-0.94247782,1),
	ObjectHistory(1119,392,-0.94247782,1),
	ObjectHistory(1123,385,-0.94247782,1),
	ObjectHistory(1126,377,-0.94247782,1),
	ObjectHistory(1128,369,-0.94247782,1),
	ObjectHistory(1130,361,-0.94247782,1),
	ObjectHistory(1131,353,-0.94247782,1),
	ObjectHistory(1132,344,-0.94247782,1),
	ObjectHistory(1132,335,-0.94247782,1),
	ObjectHistory(1132,325,-1.09955740,1),
	ObjectHistory(1131,316,-1.25663698,1),
	ObjectHistory(1129,307,-1.41371655,1),
	ObjectHistory(1127,298,-1.57079613,1),
	ObjectHistory(1124,288,-1.72787571,1),
	ObjectHistory(1121,280,-1.88495529,1),
	ObjectHistory(1116,271,-2.04203486,1),
	ObjectHistory(1112,263,-2.19911456,1),
	ObjectHistory(1107,256,-2.35619426,1),
	ObjectHistory(1101,249,-2.51327395,1),
	ObjectHistory(1095,243,-2.51327395,1),
	ObjectHistory(1089,237,-2.51327395,1),
	ObjectHistory(1082,232,-2.51327395,1),
	ObjectHistory(1075,228,-2.51327395,1),
	ObjectHistory(1067,224,-2.51327395,1),
	ObjectHistory(1060,222,-2.51327395,1),
	ObjectHistory(1051,219,-2.51327395,1),
	ObjectHistory(1043,217,-2.51327395,1),
	ObjectHistory(1033,216,-2.51327395,1),
	ObjectHistory(1024,216,-2.51327395,1),
	ObjectHistory(1014,215,-2.51327395,0),
	ObjectHistory(1005,215,-2.51327395,0),
	ObjectHistory(995,215,-2.51327395,0),
	ObjectHistory(986,215,-2.51327395,0),
	ObjectHistory(977,215,-2.51327395,0),
	ObjectHistory(967,215,-2.51327395,0),
	ObjectHistory(957,215,-2.51327395,0),
	ObjectHistory(947,215,-2.35619426,0),
	ObjectHistory(938,215,-2.19911456,0),
	ObjectHistory(929,216,-2.04203486,0),
	ObjectHistory(919,216,-1.88495529,0),
	ObjectHistory(910,217,-1.72787571,0),
	ObjectHistory(900,217,-1.57079613,0),
	ObjectHistory(891,218,-1.41371655,0),
	ObjectHistory(882,219,-1.25663698,0),
	ObjectHistory(872,220,-1.09955740,0),
	ObjectHistory(863,221,-0.94247776,0),
	ObjectHistory(853,222,-0.78539813,0),
	ObjectHistory(844,223,-0.62831849,0),
	ObjectHistory(834,224,-0.47123885,0),
	ObjectHistory(825,226,-0.31415921,0),
	ObjectHistory(815,227,-0.15707958,0),
	ObjectHistory(806,229,0.00000000,0),
	ObjectHistory(796,231,0.15707964,0),
	ObjectHistory(787,232,0.31415927,0),
	ObjectHistory(777,234,0.47123891,0),
	ObjectHistory(768,236,0.62831855,0),
	ObjectHistory(758,238,0.78539819,0),
	ObjectHistory(749,240,0.94247782,0),
	ObjectHistory(739,242,1.09955740,0),
	ObjectHistory(730,244,1.25663698,0),
	ObjectHistory(720,247,1.41371655,0),
	ObjectHistory(711,249,1.57079613,0),
	ObjectHistory(701,252,1.72787571,0),
	ObjectHistory(692,254,1.88495529,0),
	ObjectHistory(682,257,2.04203486,0),
	ObjectHistory(673,260,2.19911456,0),
	ObjectHistory(663,263,2.35619426,0),
	ObjectHistory(654,266,2.51327395,0),
	ObjectHistory(645,269,2.67035365,0),
	ObjectHistory(635,272,2.82743335,0),
	ObjectHistory(626,275,2.98451304,0),
	ObjectHistory(616,279,3.14159274,0),
	ObjectHistory(607,282,3.29867244,0),
	ObjectHistory(597,285,3.45575213,0),
	ObjectHistory(588,289,3.61283183,0),
	ObjectHistory(578,293,3.76991153,0),
	ObjectHistory(569,297,3.92699122,0),
	ObjectHistory(559,300,4.08407068,0),
	ObjectHistory(550,304,4.24115038,0),
	ObjectHistory(540,308,4.39823008,0),
	ObjectHistory(531,312,4.55530977,0),
	ObjectHistory(521,317,4.71238947,0),
	ObjectHistory(512,321,4.86946917,0),
	ObjectHistory(502,326,5.02654886,0),
	ObjectHistory(493,330,5.18362856,0),
	ObjectHistory(483,335,5.34070826,0),
	ObjectHistory(474,339,5.49778795,0),
	ObjectHistory(464,344,5.65486765,0),
	ObjectHistory(455,349,5.81194735,0),
	ObjectHistory(445,354,5.96902704,0),
	ObjectHistory(436,359,6.12610674,0),
	ObjectHistory(426,364,0.00000000,0),
	ObjectHistory(417,369,0.15707964,0),
	ObjectHistory(407,374,0.31415927,0),
	ObjectHistory(398,380,0.47123891,0),
	ObjectHistory(388,385,0.62831855,0),
	ObjectHistory(379,390,0.78539819,1),
	ObjectHistory(371,395,0.78539819,1),
	ObjectHistory(363,399,0.78539819,1),
	ObjectHistory(355,403,0.78539819,1),
	ObjectHistory(348,407,0.78539819,1),
	ObjectHistory(341,410,0.78539819,1),
	ObjectHistory(335,413,0.78539819,1),
	ObjectHistory(329,415,0.78539819,1),
	ObjectHistory(324,417,0.78539819,1),
	ObjectHistory(319,419,0.78539819,1),
	ObjectHistory(315,420,0.78539819,1),
	ObjectHistory(311,421,0.78539819,1),
	ObjectHistory(308,421,0.78539819,1),
	ObjectHistory(305,422,0.78539819,1),
	ObjectHistory(303,421,0.78539819,1),
	ObjectHistory(301,421,0.78539819,1),
	ObjectHistory(299,420,0.78539819,1),
	ObjectHistory(298,418,0.78539819,1),
	ObjectHistory(298,416,0.78539819,1),
	ObjectHistory(298,414,0.78539819,1),
	ObjectHistory(298,412,0.78539819,1),
	ObjectHistory(299,409,0.78539819,1),
	ObjectHistory(300,405,0.78539819,1),
	ObjectHistory(302,402,0.78539819,1),
	ObjectHistory(304,397,0.78539819,1),
	ObjectHistory(307,393,0.78539819,1),
	ObjectHistory(310,388,0.78539819,1),
	ObjectHistory(314,383,0.78539819,1),
	ObjectHistory(318,377,0.78539819,1),
	ObjectHistory(323,371,0.78539819,1),
	ObjectHistory(328,365,0.78539819,1),
	ObjectHistory(334,358,0.78539819,1),
	ObjectHistory(340,351,0.94247782,1),
	ObjectHistory(347,344,1.09955740,1),
	ObjectHistory(354,336,1.25663698,1),
	ObjectHistory(362,329,1.41371655,1),
	ObjectHistory(371,322,1.57079613,1),
	ObjectHistory(380,314,1.72787571,1),
	ObjectHistory(390,308,1.88495529,1),
	ObjectHistory(401,301,2.04203486,1),
	ObjectHistory(412,295,2.19911456,1),
	ObjectHistory(424,289,2.35619426,1),
	ObjectHistory(435,285,2.51327395,1),
	ObjectHistory(447,281,2.67035365,1),
	ObjectHistory(460,277,2.82743335,1),
	ObjectHistory(472,275,2.98451304,1),
	ObjectHistory(485,273,2.98451304,1),
	ObjectHistory(498,272,2.98451304,1),
	ObjectHistory(511,271,2.98451304,1),
	ObjectHistory(524,272,2.98451304,1),
	ObjectHistory(537,273,2.98451304,1),
	ObjectHistory(550,275,2.98451304,1),
	ObjectHistory(564,277,2.98451304,1),
	ObjectHistory(578,281,2.98451304,1),
	ObjectHistory(591,285,2.98451304,1),
	ObjectHistory(604,289,2.98451304,1),
	ObjectHistory(618,295,2.98451304,1),
	ObjectHistory(632,302,2.98451304,1),
	ObjectHistory(646,309,3.14159274,1),
	ObjectHistory(659,316,3.29867244,1),
	ObjectHistory(673,325,3.45575213,1),
	ObjectHistory(686,334,3.61283183,1),
	ObjectHistory(699,344,3.76991153,1),
	ObjectHistory(711,354,3.92699122,1),
	ObjectHistory(723,365,4.08407068,1),
	ObjectHistory(734,376,4.24115038,1),
	ObjectHistory(745,388,4.39823008,1),
	ObjectHistory(755,400,4.55530977,1),
	ObjectHistory(764,412,4.71238947,1),
	ObjectHistory(773,423,4.86946917,1),
	ObjectHistory(781,436,5.02654886,1),
	ObjectHistory(789,448,5.18362856,1),
	ObjectHistory(796,459,5.34070826,1),
	ObjectHistory(802,470,5.49778795,1),
	ObjectHistory(807,479,5.65486765,1),
	ObjectHistory(813,490,5.81194735,1),
	ObjectHistory(819,499,5.96902704,1),
	ObjectHistory(824,507,6.12610674,1),
	ObjectHistory(829,516,0.00000000,1),
	ObjectHistory(835,523,0.15707964,1),
	ObjectHistory(840,530,0.31415927,1),
	ObjectHistory(846,537,0.31415927,1),
	ObjectHistory(853,543,0.31415927,1),
	ObjectHistory(859,548,0.31415927,1),
	ObjectHistory(866,554,0.31415927,1),
	ObjectHistory(872,558,0.31415927,1),
	ObjectHistory(879,562,0.31415927,1),
	ObjectHistory(886,565,0.31415927,1),
	ObjectHistory(893,568,0.31415927,1),
	ObjectHistory(901,570,0.31415927,1),
	ObjectHistory(909,571,0.31415927,1),
	ObjectHistory(917,572,0.31415927,1),
	ObjectHistory(925,573,0.31415927,1),
	ObjectHistory(933,573,0.31415927,1),
	ObjectHistory(942,572,0.15707964,1),
	ObjectHistory(950,571,0.00000000,1),
	ObjectHistory(959,570,-0.15707964,1),
	ObjectHistory(967,567,-0.31415927,1),
	ObjectHistory(974,565,-0.47123891,1),
	ObjectHistory(982,561,-0.62831855,1),
	ObjectHistory(989,558,-0.78539819,1),
	ObjectHistory(996,554,-0.94247782,1),
	ObjectHistory(1001,550,-1.09955740,1),
	ObjectHistory(1007,545,-1.25663698,1),
	ObjectHistory(1011,541,-1.41371655,1),
	ObjectHistory(1015,537,-1.57079613,1),
	ObjectHistory(1018,533,-1.72787571,1),
	ObjectHistory(1021,529,-1.88495529,1),
	ObjectHistory(1023,526,-1.88495529,1),
	ObjectHistory(1024,523,-1.88495529,1),
	ObjectHistory(1025,520,-1.88495529,1),
	ObjectHistory(1025,518,-1.88495529,1),
	ObjectHistory(1024,515,-1.88495529,1),
	ObjectHistory(1023,513,-1.88495529,1),
	ObjectHistory(1021,512,-1.88495529,1),
	ObjectHistory(1019,511,-1.88495529,1),
	ObjectHistory(1016,510,-1.88495529,1),
	ObjectHistory(1012,509,-1.88495529,1),
	ObjectHistory(1008,508,-1.88495529,1),
	ObjectHistory(1003,508,-1.88495529,1),
	ObjectHistory(997,508,-1.88495529,1),
	ObjectHistory(991,509,-1.88495529,1),
	ObjectHistory(984,510,-1.88495529,1),
	ObjectHistory(977,511,-1.88495529,1),
	ObjectHistory(968,512,-1.88495529,1),
	ObjectHistory(959,514,-1.72787571,1),
	ObjectHistory(950,515,-1.57079613,1),
	ObjectHistory(940,517,-1.41371655,1),
	ObjectHistory(929,518,-1.25663698,1),
	ObjectHistory(917,520,-1.09955740,1),
	ObjectHistory(906,520,-0.94247776,1),
	ObjectHistory(893,521,-0.78539813,1),
	ObjectHistory(880,521,-0.62831849,1),
	ObjectHistory(867,521,-0.47123885,1),
	ObjectHistory(854,520,-0.31415921,1),
	ObjectHistory(841,519,-0.15707958,1),
	ObjectHistory(827,516,0.00000000,1),
	ObjectHistory(814,514,0.15707964,1),
	ObjectHistory(801,511,0.31415927,1),
	ObjectHistory(788,507,0.47123891,1),
	ObjectHistory(775,503,0.62831855,1),
	ObjectHistory(764,498,0.78539819,1),
	ObjectHistory(753,494,0.94247782,1),
	ObjectHistory(741,488,1.09955740,1),
	ObjectHistory(731,483,1.25663698,1),
	ObjectHistory(722,478,1.41371655,1),
	ObjectHistory(713,473,1.57079613,1),
	ObjectHistory(705,469,1.72787571,1),
	ObjectHistory(701,466,1.88495529,1),
	ObjectHistory(694,462,2.04203486,1),
	ObjectHistory(688,458,2.19911456,1),
	ObjectHistory(682,455,2.19911456,1),
	ObjectHistory(677,452,2.19911456,1),
	ObjectHistory(672,449,2.19911456,1),
	ObjectHistory(668,448,2.19911456,1),
	ObjectHistory(664,446,2.19911456,1),
	ObjectHistory(661,446,2.19911456,1),
	ObjectHistory(658,445,2.19911456,1),
	ObjectHistory(656,445,2.19911456,1),
	ObjectHistory(655,446,2.19911456,1),
	ObjectHistory(653,447,2.19911456,1),
	ObjectHistory(653,448,2.19911456,1),
	ObjectHistory(653,450,2.19911456,1),
	ObjectHistory(654,453,2.19911456,1),
	ObjectHistory(655,456,2.19911456,1),
	ObjectHistory(656,459,2.19911456,1),
	ObjectHistory(659,463,2.04203486,1),
	ObjectHistory(662,467,1.88495529,1),
	ObjectHistory(665,472,1.72787571,1),
	ObjectHistory(670,476,1.57079613,1),
	ObjectHistory(674,480,1.41371655,1),
	ObjectHistory(680,485,1.25663698,1),
	ObjectHistory(686,489,1.09955740,1),
	ObjectHistory(693,493,0.94247776,1),
	ObjectHistory(700,496,0.78539813,1),
	ObjectHistory(707,499,0.62831849,1),
	ObjectHistory(715,501,0.47123885,1),
	ObjectHistory(723,503,0.31415921,1),
	ObjectHistory(731,505,0.15707958,1),
	ObjectHistory(740,505,0.00000000,1),
	ObjectHistory(748,505,-0.15707964,1),
	ObjectHistory(755,505,-0.31415927,1),
	ObjectHistory(763,504,-0.47123891,1),
	ObjectHistory(770,503,-0.47123891,1),
	ObjectHistory(777,501,-0.47123891,1),
	ObjectHistory(784,499,-0.47123891,1),
	ObjectHistory(790,496,-0.47123891,1),
	ObjectHistory(796,492,-0.47123891,1),
	ObjectHistory(802,488,-0.47123891,1),
	ObjectHistory(807,484,-0.47123891,1),
	ObjectHistory(812,479,-0.47123891,1),
	ObjectHistory(817,474,-0.47123891,1),
	ObjectHistory(821,468,-0.47123891,1),
	ObjectHistory(826,461,-0.47123891,1),
	ObjectHistory(830,454,-0.47123891,1),
	ObjectHistory(833,447,-0.47123891,1),
	ObjectHistory(837,439,-0.47123891,1),
	ObjectHistory(839,431,-0.47123891,1),
	ObjectHistory(842,421,-0.47123891,1),
	ObjectHistory(845,412,-0.47123891,1),
	ObjectHistory(847,402,-0.62831855,1),
	ObjectHistory(848,392,-0.78539819,1),
	ObjectHistory(849,381,-0.94247782,1),
	ObjectHistory(849,370,-1.09955740,1),
	ObjectHistory(849,360,-1.25663698,1),
	ObjectHistory(848,348,-1.41371655,1),
	ObjectHistory(847,338,-1.57079613,1),
	ObjectHistory(845,327,-1.72787571,1),
	ObjectHistory(842,316,-1.88495529,1),
	ObjectHistory(838,307,-2.04203486,1),
	ObjectHistory(834,297,-2.19911456,1),
	ObjectHistory(830,288,-2.35619426,1),
	ObjectHistory(825,280,-2.51327395,1),
	ObjectHistory(820,272,-2.67035365,1),
	ObjectHistory(814,265,-2.82743335,1),
	ObjectHistory(809,259,-2.82743335,1),
	ObjectHistory(803,254,-2.82743335,1),
	ObjectHistory(797,249,-2.82743335,1),
	ObjectHistory(791,245,-2.82743335,1),
	ObjectHistory(784,242,-2.82743335,1),
	ObjectHistory(778,239,-2.82743335,1),
	ObjectHistory(771,237,-2.82743335,1),
	ObjectHistory(764,236,-2.82743335,1),
	ObjectHistory(757,236,-2.82743335,1),
	ObjectHistory(749,236,-2.82743335,1),
	ObjectHistory(742,237,-2.67035365,1),
	ObjectHistory(733,239,-2.51327395,1),
	ObjectHistory(725,241,-2.35619426,1),
	ObjectHistory(716,244,-2.19911456,1),
	ObjectHistory(706,247,-2.04203486,1),
	ObjectHistory(696,250,-1.88495529,1),
	ObjectHistory(685,254,-1.72787571,1),
	ObjectHistory(673,257,-1.57079613,1),
	ObjectHistory(660,261,-1.41371655,1),
	ObjectHistory(648,264,-1.25663698,1),
	ObjectHistory(633,268,-1.09955740,1),
	ObjectHistory(620,271,-0.94247776,1),
	ObjectHistory(605,273,-0.78539813,1),
	ObjectHistory(590,276,-0.62831849,1),
	ObjectHistory(575,278,-0.47123885,0),
	ObjectHistory(560,280,-0.31415921,0),
	ObjectHistory(546,282,-0.15707958,0),
	ObjectHistory(531,285,0.00000000,0),
	ObjectHistory(516,288,0.15707964,0),
	ObjectHistory(501,290,0.31415927,0),
	ObjectHistory(485,293,0.47123891,0),
	ObjectHistory(470,296,0.62831855,0),
	ObjectHistory(455,299,0.78539819,0),
	ObjectHistory(440,302,0.94247782,0),
	ObjectHistory(426,305,1.09955740,0),
	ObjectHistory(411,308,1.25663698,0),
	ObjectHistory(395,312,1.41371655,0),
	ObjectHistory(380,315,1.57079613,0),
	ObjectHistory(365,319,1.72787571,0),
	ObjectHistory(351,322,1.88495529,0),
	ObjectHistory(336,326,2.04203486,0),
	ObjectHistory(321,330,2.19911456,0),
	ObjectHistory(305,334,2.35619426,0),
	ObjectHistory(290,338,2.51327395,0),
	ObjectHistory(276,342,2.67035365,0),
	ObjectHistory(261,345,2.82743335,0),
	ObjectHistory(245,350,2.98451304,0),
	ObjectHistory(230,354,3.14159274,0),
	ObjectHistory(216,358,3.29867244,0),
	ObjectHistory(201,363,3.45575213,0),
	ObjectHistory(186,367,3.61283183,0),
	ObjectHistory(171,372,3.76991153,0),
	ObjectHistory(156,377,3.92699122,0),
	ObjectHistory(141,382,4.08407068,0),
	ObjectHistory(126,387,4.24115038,0),
	ObjectHistory(111,392,4.39823008,0),
	ObjectHistory(96,397,4.55530977,0),
	ObjectHistory(81,402,4.71238947,0),
	ObjectHistory(66,407,4.86946917,0),
	ObjectHistory(51,413,5.02654886,0),
	ObjectHistory(36,418,5.18362856,0),
	ObjectHistory(21,424,5.34070826,0),
	ObjectHistory(6,429,5.49778795,0),
	ObjectHistory(764,309,-1.72787571,1),
	ObjectHistory(768,301,-1.88495529,1),
	ObjectHistory(771,292,-2.04203486,1),
	ObjectHistory(600,377,0.15707964,0),
	ObjectHistory(600,374,0.31415927,0),
	ObjectHistory(600,371,0.47123891,0),
	ObjectHistory(600,368,0.62831855,0),
	ObjectHistory(600,366,0.78539819,0),
	ObjectHistory(600,363,0.94247782,0),
	ObjectHistory(600,361,0.94247782,0),
	ObjectHistory(600,359,0.94247782,0),
	ObjectHistory(600,357,0.94247782,0),
	ObjectHistory(600,355,0.94247782,0),
	ObjectHistory(600,352,0.94247782,0),
	ObjectHistory(600,350,0.94247782,0),
	ObjectHistory(600,349,0.94247782,0),
	ObjectHistory(600,347,0.94247782,0),
	ObjectHistory(600,345,0.94247782,0),
	ObjectHistory(600,344,0.94247782,0),
	ObjectHistory(600,342,0.94247782,0),
	ObjectHistory(600,341,0.94247782,0),
	ObjectHistory(600,339,0.94247782,0),
	ObjectHistory(600,338,0.94247782,0),
	ObjectHistory(600,337,0.94247782,0),
	ObjectHistory(600,336,0.94247782,0),
	ObjectHistory(600,335,0.94247782,0),
	ObjectHistory(600,334,0.94247782,0),
	ObjectHistory(600,333,0.94247782,0),
	ObjectHistory(600,333,0.94247782,0),
	ObjectHistory(600,332,0.94247782,0),
	ObjectHistory(600,332,0.94247782,0),
	ObjectHistory(600,331,0.94247782,0),
	ObjectHistory(600,331,0.94247782,0),
	ObjectHistory(600,331,0.94247782,0),
	ObjectHistory(600,330,0.94247782,0),
	ObjectHistory(600,330,0.94247782,0),
	ObjectHistory(600,330,0.94247782,0),
	ObjectHistory(610,353,-0.31415921,1),
	ObjectHistory(598,357,-0.15707958,1),
	ObjectHistory(585,360,0.00000000,1),
	ObjectHistory(573,363,0.15707964,1),
	ObjectHistory(561,365,0.31415927,1),
	ObjectHistory(548,367,0.47123891,1),
	ObjectHistory(537,368,0.62831855,1),
	ObjectHistory(525,369,0.62831855,1),
	ObjectHistory(514,370,0.62831855,1),
	ObjectHistory(503,369,0.62831855,1),
	ObjectHistory(494,369,0.62831855,1),
	ObjectHistory(484,368,0.62831855,1),
	ObjectHistory(475,367,0.62831855,1),
	ObjectHistory(466,365,0.62831855,1),
	ObjectHistory(457,362,0.62831855,1),
	ObjectHistory(449,360,0.62831855,1),
	ObjectHistory(441,356,0.62831855,1),
	ObjectHistory(434,353,0.62831855,1),
	ObjectHistory(427,349,0.62831855,1),
	ObjectHistory(421,344,0.62831855,1),
	ObjectHistory(414,339,0.62831855,1),
	ObjectHistory(409,334,0.62831855,1),
	ObjectHistory(403,328,0.47123891,1),
	ObjectHistory(398,321,0.31415927,1),
	ObjectHistory(393,314,0.15707964,1),
	ObjectHistory(387,306,0.00000000,1),
	ObjectHistory(382,298,-0.15707964,1),
	ObjectHistory(377,289,-0.31415927,1),
	ObjectHistory(371,280,-0.47123891,1),
	ObjectHistory(365,270,-0.62831855,1),
	ObjectHistory(358,260,-0.78539819,0),
	ObjectHistory(352,251,-0.94247782,0),
	ObjectHistory(346,241,-1.09955740,0),
	ObjectHistory(340,232,-1.25663698,0),
	ObjectHistory(334,223,-1.41371655,0),
	ObjectHistory(327,213,-1.57079613,0),
	ObjectHistory(322,205,-1.72787571,0),
	ObjectHistory(315,195,-1.88495529,0),
	ObjectHistory(309,186,-2.04203486,0),
	ObjectHistory(303,178,-2.19911456,0),
	ObjectHistory(297,169,-2.35619426,0),
	ObjectHistory(290,160,-2.51327395,0),
	ObjectHistory(284,151,-2.67035365,0),
	ObjectHistory(278,143,-2.82743335,0),
	ObjectHistory(272,135,-2.98451304,1),
	ObjectHistory(265,128,-3.14159274,1),
	ObjectHistory(259,122,-3.14159274,1),
	ObjectHistory(253,116,-3.14159274,1),
	ObjectHistory(246,112,-3.14159274,1),
	ObjectHistory(240,108,-3.14159274,1),
	ObjectHistory(234,104,-3.14159274,1),
	ObjectHistory(227,102,-3.14159274,1),
	ObjectHistory(221,100,-3.14159274,1),
	ObjectHistory(215,99,-3.14159274,1),
	ObjectHistory(209,99,-3.14159274,1),
	ObjectHistory(203,100,-3.14159274,1),
	ObjectHistory(196,101,-3.14159274,1),
	ObjectHistory(190,103,-3.14159274,1),
	ObjectHistory(184,106,-3.14159274,1),
	ObjectHistory(177,110,-3.14159274,1),
	ObjectHistory(171,114,-3.14159274,1),
	ObjectHistory(165,119,-3.14159274,1),
	ObjectHistory(159,125,-3.29867244,1),
	ObjectHistory(153,131,-3.45575213,1),
	ObjectHistory(147,139,-3.61283183,1),
	ObjectHistory(142,146,-3.76991153,1),
	ObjectHistory(137,155,-3.92699122,1),
	ObjectHistory(132,164,-4.08407068,1),
	ObjectHistory(129,173,-4.24115038,1),
	ObjectHistory(126,183,-4.39823008,1),
	ObjectHistory(123,193,-4.55530977,1),
	ObjectHistory(122,202,-4.71238947,1),
	ObjectHistory(121,212,-4.86946917,1),
	ObjectHistory(120,222,-5.02654886,1),
	ObjectHistory(120,231,-5.18362856,1),
	ObjectHistory(121,241,-5.34070826,1),
	ObjectHistory(122,250,-5.49778795,1),
	ObjectHistory(124,258,-5.65486765,1),
	ObjectHistory(126,266,-5.65486765,1),
	ObjectHistory(128,274,-5.65486765,1),
	ObjectHistory(131,281,-5.65486765,1),
	ObjectHistory(134,287,-5.65486765,1),
	ObjectHistory(137,293,-5.65486765,1),
	ObjectHistory(141,299,-5.65486765,1),
	ObjectHistory(146,304,-5.65486765,1),
	ObjectHistory(150,309,-5.65486765,1),
	ObjectHistory(156,314,-5.65486765,1),
	ObjectHistory(161,317,-5.65486765,1),
	ObjectHistory(167,321,-5.65486765,1),
	ObjectHistory(173,324,-5.65486765,1),
	ObjectHistory(180,326,-5.65486765,1),
	ObjectHistory(187,328,-5.65486765,1),
	ObjectHistory(194,330,-5.65486765,1),
	ObjectHistory(202,331,-5.65486765,1),
	ObjectHistory(210,332,-5.65486765,1),
	ObjectHistory(219,332,-5.65486765,1),
	ObjectHistory(228,332,-5.49778795,1),
	ObjectHistory(237,332,-5.34070826,1),
	ObjectHistory(248,331,-5.18362856,1),
	ObjectHistory(259,330,-5.02654886,1),
	ObjectHistory(270,330,-4.86946917,1),
	ObjectHistory(282,329,-4.71238947,1),
	ObjectHistory(295,328,-4.55530977,0),
	ObjectHistory(307,328,-4.39823008,0),
	ObjectHistory(319,328,-4.24115038,0),
	ObjectHistory(331,327,-4.08407068,0),
	ObjectHistory(343,327,-3.92699099,0),
	ObjectHistory(356,327,-3.76991129,0),
	ObjectHistory(368,327,-3.61283159,0),
	ObjectHistory(380,327,-3.45575190,0),
	ObjectHistory(392,327,-3.29867220,0),
	ObjectHistory(404,327,-3.14159250,0),
	ObjectHistory(416,328,-2.98451281,0),
	ObjectHistory(428,328,-2.82743311,0),
	ObjectHistory(441,329,-2.67035341,0),
	ObjectHistory(453,329,-2.51327372,0),
	ObjectHistory(465,330,-2.35619402,0),
	ObjectHistory(477,331,-2.19911432,0),
	ObjectHistory(489,332,-2.04203463,0),
	ObjectHistory(501,333,-1.88495505,0),
	ObjectHistory(514,334,-1.72787547,0),
	ObjectHistory(526,335,-1.57079589,0),
	ObjectHistory(538,336,-1.41371632,0),
	ObjectHistory(550,337,-1.25663674,0),
	ObjectHistory(563,339,-1.09955716,0),
	ObjectHistory(575,340,-0.94247752,0),
	ObjectHistory(587,342,-0.78539789,0),
	ObjectHistory(599,343,-0.62831825,0),
	ObjectHistory(611,345,-0.47123861,0),
	ObjectHistory(623,347,-0.31415898,0),
	ObjectHistory(636,349,-0.15707934,0),
	ObjectHistory(648,351,0.00000000,0),
	ObjectHistory(660,353,0.15707964,0),
	ObjectHistory(672,355,0.31415927,0),
	ObjectHistory(684,357,0.47123891,0),
	ObjectHistory(697,360,0.62831855,0),
	ObjectHistory(709,362,0.78539819,0),
	ObjectHistory(721,365,0.94247782,0),
	ObjectHistory(734,367,0.94247782,1),
	ObjectHistory(748,369,0.94247782,1),
	ObjectHistory(761,371,0.94247782,1),
	ObjectHistory(776,372,0.94247782,1),
	ObjectHistory(790,373,0.78539819,1),
	ObjectHistory(805,374,0.62831855,1),
	ObjectHistory(820,374,0.47123891,1),
	ObjectHistory(836,373,0.31415927,1),
	ObjectHistory(852,372,0.15707964,1),
	ObjectHistory(868,370,0.00000000,1),
	ObjectHistory(883,368,-0.15707964,1),
	ObjectHistory(899,365,-0.31415927,1),
	ObjectHistory(914,362,-0.47123891,1),
	ObjectHistory(929,358,-0.62831855,1),
	ObjectHistory(944,354,-0.78539819,1),
	ObjectHistory(957,349,-0.94247782,1),
	ObjectHistory(971,345,-1.09955740,1),
	ObjectHistory(983,340,-1.25663698,1),
	ObjectHistory(995,335,-1.41371655,1),
	ObjectHistory(1006,331,-1.57079613,1),
	ObjectHistory(1017,326,-1.72787571,1),
	ObjectHistory(1027,322,-1.88495529,1),
	ObjectHistory(1037,318,-2.04203486,1),
	ObjectHistory(1045,314,-2.04203486,1),
	ObjectHistory(1053,311,-2.04203486,1),
	ObjectHistory(1061,309,-2.04203486,1),
	ObjectHistory(1068,306,-2.04203486,1),
	ObjectHistory(1074,305,-2.04203486,1),
	ObjectHistory(1080,303,-2.04203486,1),
	ObjectHistory(1085,302,-2.04203486,1),
	ObjectHistory(1090,301,-2.04203486,1),
	ObjectHistory(1094,301,-2.04203486,1),
	ObjectHistory(1097,301,-2.04203486,1),
	ObjectHistory(1100,302,-2.04203486,1),
	ObjectHistory(1102,303,-2.04203486,1),
	ObjectHistory(1104,304,-2.04203486,1),
	ObjectHistory(1105,305,-2.04203486,1),
	ObjectHistory(1105,307,-2.04203486,1),
	ObjectHistory(1105,310,-2.04203486,1),
	ObjectHistory(1104,313,-2.04203486,1),
	ObjectHistory(1103,316,-2.04203486,1),
	ObjectHistory(1101,320,-2.04203486,1),
	ObjectHistory(1099,324,-2.04203486,1),
	ObjectHistory(1095,328,-2.04203486,1),
	ObjectHistory(1092,333,-2.04203486,1),
	ObjectHistory(1087,338,-2.04203486,1),
	ObjectHistory(1083,343,-1.88495529,1),
	ObjectHistory(1077,349,-1.72787571,1),
	ObjectHistory(1071,355,-1.57079613,1),
	ObjectHistory(1064,361,-1.41371655,1),
	ObjectHistory(1056,366,-1.25663698,1),
	ObjectHistory(1048,372,-1.09955740,1),
	ObjectHistory(1039,377,-0.94247776,1),
	ObjectHistory(1031,381,-0.78539813,1),
	ObjectHistory(1021,386,-0.62831849,1),
	ObjectHistory(1011,389,-0.47123885,1),
	ObjectHistory(1001,393,-0.31415921,1),
	ObjectHistory(991,395,-0.15707958,1),
	ObjectHistory(981,397,0.00000000,1),
	ObjectHistory(971,399,0.15707964,1),
	ObjectHistory(961,400,0.15707964,1),
	ObjectHistory(951,400,0.15707964,1),
	ObjectHistory(941,400,0.15707964,1),
	ObjectHistory(931,400,0.15707964,0),
	ObjectHistory(921,400,0.15707964,0),
	ObjectHistory(912,400,0.15707964,0),
	ObjectHistory(902,400,0.15707964,0),
	ObjectHistory(892,400,0.15707964,0),
	ObjectHistory(883,401,0.15707964,0),
	ObjectHistory(873,401,0.15707964,0),
	ObjectHistory(863,402,0.15707964,0),
	ObjectHistory(853,402,0.15707964,0),
	ObjectHistory(844,403,0.31415927,0),
	ObjectHistory(834,404,0.47123891,0),
	ObjectHistory(824,404,0.62831855,0),
	ObjectHistory(814,405,0.78539819,0),
	ObjectHistory(805,406,0.94247782,0),
	ObjectHistory(795,408,1.09955740,0),
	ObjectHistory(785,409,1.25663698,0),
	ObjectHistory(775,410,1.41371655,0),
	ObjectHistory(766,412,1.57079613,0),
	ObjectHistory(756,413,1.72787571,0),
	ObjectHistory(746,415,1.88495529,0),
	ObjectHistory(737,416,2.04203486,0),
	ObjectHistory(727,418,2.19911456,0),
	ObjectHistory(717,420,2.35619426,0),
	ObjectHistory(707,422,2.51327395,0),
	ObjectHistory(697,424,2.67035365,0),
	ObjectHistory(688,426,2.82743335,0),
	ObjectHistory(678,428,2.98451304,0),
	ObjectHistory(668,430,3.14159274,0),
	ObjectHistory(659,433,3.29867244,0),
	ObjectHistory(649,435,3.45575213,0),
	ObjectHistory(639,438,3.61283183,0),
	ObjectHistory(629,440,3.76991153,0),
	ObjectHistory(619,443,3.92699122,0),
	ObjectHistory(610,446,4.08407068,0),
	ObjectHistory(600,449,4.24115038,0),
	ObjectHistory(590,452,4.39823008,0),
	ObjectHistory(581,455,4.55530977,0),
	ObjectHistory(571,458,4.71238947,0),
	ObjectHistory(561,462,4.86946917,0),
	ObjectHistory(551,465,5.02654886,0),
	ObjectHistory(542,468,5.18362856,0),
	ObjectHistory(532,472,5.34070826,0),
	ObjectHistory(522,475,5.49778795,0),
	ObjectHistory(512,479,5.65486765,0),
	ObjectHistory(503,483,5.81194735,0),
	ObjectHistory(493,487,5.96902704,0),
	ObjectHistory(483,491,6.12610674,0),
	ObjectHistory(473,495,0.00000000,0),
	ObjectHistory(464,499,0.15707964,0),
	ObjectHistory(454,503,0.31415927,1),
	ObjectHistory(445,506,0.31415927,1),
	ObjectHistory(436,508,0.31415927,1),
	ObjectHistory(427,510,0.31415927,1),
	ObjectHistory(418,512,0.31415927,1),
	ObjectHistory(409,512,0.31415927,1),
	ObjectHistory(401,513,0.31415927,1),
	ObjectHistory(393,513,0.31415927,1),
	ObjectHistory(385,512,0.31415927,1),
	ObjectHistory(378,511,0.31415927,1),
	ObjectHistory(370,509,0.31415927,1),
	ObjectHistory(363,506,0.31415927,1),
	ObjectHistory(356,503,0.31415927,1),
	ObjectHistory(349,500,0.31415927,1),
	ObjectHistory(342,496,0.31415927,1),
	ObjectHistory(336,491,0.31415927,1),
	ObjectHistory(329,486,0.31415927,1),
	ObjectHistory(323,480,0.31415927,1),
	ObjectHistory(318,474,0.31415927,1),
	ObjectHistory(312,468,0.31415927,1),
	ObjectHistory(306,461,0.31415927,0),
	ObjectHistory(301,454,0.31415927,0),
	ObjectHistory(295,448,0.31415927,0),
	ObjectHistory(289,442,0.31415927,0),
	ObjectHistory(284,435,0.31415927,0),
	ObjectHistory(278,429,0.31415927,0),
	ObjectHistory(273,423,0.31415927,0),
	ObjectHistory(267,417,0.31415927,0),
	ObjectHistory(261,411,0.47123891,0),
	ObjectHistory(256,405,0.62831855,0),
	ObjectHistory(250,399,0.78539819,0),
	ObjectHistory(244,394,0.94247782,0),
	ObjectHistory(239,388,1.09955740,0),
	ObjectHistory(233,383,1.25663698,0),
	ObjectHistory(227,377,1.25663698,0),
	ObjectHistory(222,372,1.25663698,0),
	ObjectHistory(216,366,1.25663698,0),
	ObjectHistory(211,361,1.25663698,0),
	ObjectHistory(206,356,1.25663698,1),
	ObjectHistory(201,351,1.25663698,1),
	ObjectHistory(198,345,1.25663698,1),
	ObjectHistory(195,340,1.25663698,1),
	ObjectHistory(192,334,1.25663698,1),
	ObjectHistory(190,328,1.25663698,1),
	ObjectHistory(189,322,1.25663698,1),
	ObjectHistory(188,316,1.25663698,1),
	ObjectHistory(189,310,1.25663698,1),
	ObjectHistory(189,303,1.25663698,1),
	ObjectHistory(191,297,1.25663698,1),
	ObjectHistory(193,290,1.25663698,1),
	ObjectHistory(195,284,1.25663698,1),
	ObjectHistory(199,277,1.41371655,1),
	ObjectHistory(202,270,1.57079613,1),
	ObjectHistory(207,264,1.72787571,1),
	ObjectHistory(212,258,1.88495529,1),
	ObjectHistory(218,252,2.04203486,1),
	ObjectHistory(224,247,2.19911456,1),
	ObjectHistory(231,242,2.35619426,1),
	ObjectHistory(238,238,2.51327395,1),
	ObjectHistory(246,235,2.67035365,1),
	ObjectHistory(254,232,2.82743335,1),
	ObjectHistory(261,230,2.98451304,1),
	ObjectHistory(270,229,2.98451304,1),
	ObjectHistory(278,229,2.98451304,1),
	ObjectHistory(286,229,2.98451304,1),
	ObjectHistory(294,230,2.98451304,1),
	ObjectHistory(303,232,2.98451304,1),
	ObjectHistory(311,235,2.98451304,1),
	ObjectHistory(319,238,2.98451304,1),
	ObjectHistory(328,242,2.82743335,1),
	ObjectHistory(337,247,2.67035365,1),
	ObjectHistory(347,252,2.51327395,1),
	ObjectHistory(357,258,2.35619426,1),
	ObjectHistory(367,264,2.19911456,1),
	ObjectHistory(378,271,2.04203486,1),
	ObjectHistory(390,279,1.88495529,1),
	ObjectHistory(403,286,1.72787571,1),
	ObjectHistory(416,293,1.57079613,1),
	ObjectHistory(429,300,1.41371655,1),
	ObjectHistory(444,308,1.25663698,1),
	ObjectHistory(459,315,1.09955740,1),
	ObjectHistory(474,321,0.94247776,1),
	ObjectHistory(490,328,0.78539813,1),
	ObjectHistory(506,334,0.62831849,1),
	ObjectHistory(524,339,0.47123885,1),
	ObjectHistory(540,344,0.31415921,1),
	ObjectHistory(556,348,0.15707958,1),
	ObjectHistory(573,352,0.00000000,1),
	ObjectHistory(590,355,-0.15707964,1),
	ObjectHistory(607,358,-0.31415927,1),
	ObjectHistory(624,360,-0.31415927,1),
	ObjectHistory(639,362,-0.31415927,0),
	ObjectHistory(657,364,-0.31415927,0),
	ObjectHistory(672,366,-0.31415927,0),
	ObjectHistory(689,369,-0.31415927,0),
	ObjectHistory(705,371,-0.31415927,0),
	ObjectHistory(722,374,-0.31415927,0),
	ObjectHistory(738,377,-0.31415927,0),
	ObjectHistory(755,379,-0.31415927,0),
	ObjectHistory(771,382,-0.31415927,0),
	ObjectHistory(788,385,-0.31415927,0),
	ObjectHistory(804,388,-0.31415927,0),
	ObjectHistory(821,391,-0.31415927,0),
	ObjectHistory(836,394,-0.31415927,0),
	ObjectHistory(853,398,-0.31415927,0),
	ObjectHistory(870,401,-0.47123891,0),
	ObjectHistory(886,404,-0.62831855,0),
	ObjectHistory(902,408,-0.78539819,1),
	ObjectHistory(917,410,-0.78539819,1),
	ObjectHistory(933,413,-0.78539819,1),
	ObjectHistory(947,414,-0.78539819,1),
	ObjectHistory(961,416,-0.78539819,1),
	ObjectHistory(975,417,-0.78539819,1),
	ObjectHistory(988,418,-0.78539819,1),
	ObjectHistory(1000,418,-0.78539819,1),
	ObjectHistory(1013,418,-0.78539819,1),
	ObjectHistory(1024,418,-0.78539819,1),
	ObjectHistory(1036,417,-0.78539819,1),
	ObjectHistory(1046,416,-0.78539819,1),
	ObjectHistory(1057,415,-0.78539819,1),
	ObjectHistory(1067,413,-0.78539819,1),
	ObjectHistory(1076,410,-0.78539819,1),
	ObjectHistory(1085,408,-0.78539819,1),
	ObjectHistory(1093,405,-0.78539819,1),
	ObjectHistory(1101,401,-0.78539819,1),
	ObjectHistory(1109,397,-0.94247782,1),
	ObjectHistory(1116,394,-1.09955740,1),
	ObjectHistory(1121,390,-1.25663698,1),
	ObjectHistory(1127,386,-1.41371655,1),
	ObjectHistory(1132,382,-1.41371655,1),
	ObjectHistory(1136,377,-1.41371655,1),
	ObjectHistory(1140,373,-1.41371655,1),
	ObjectHistory(1143,369,-1.41371655,1),
	ObjectHistory(1145,365,-1.41371655,1),
	ObjectHistory(1146,361,-1.41371655,1),
	ObjectHistory(1147,356,-1.41371655,1),
	ObjectHistory(1148,352,-1.41371655,1),
	ObjectHistory(1147,348,-1.41371655,1),
	ObjectHistory(1146,344,-1.41371655,1),
	ObjectHistory(1144,340,-1.41371655,1),
	ObjectHistory(1142,335,-1.41371655,1),
	ObjectHistory(1139,331,-1.41371655,1),
	ObjectHistory(1135,327,-1.41371655,1),
	ObjectHistory(1131,323,-1.41371655,1),
	ObjectHistory(1126,318,-1.41371655,1),
	ObjectHistory(1120,314,-1.41371655,1),
	ObjectHistory(1114,310,-1.41371655,1),
	ObjectHistory(1107,305,-1.41371655,1),
	ObjectHistory(1099,301,-1.57079613,1),
	ObjectHistory(1090,297,-1.72787571,1),
	ObjectHistory(1082,293,-1.88495529,1),
	ObjectHistory(1072,290,-2.04203486,1),
	ObjectHistory(1062,287,-2.19911456,1),
	ObjectHistory(1052,284,-2.35619426,1),
	ObjectHistory(1041,282,-2.51327395,0),
	ObjectHistory(1031,280,-2.67035365,0),
	ObjectHistory(1020,278,-2.82743335,0),
	ObjectHistory(1010,276,-2.98451304,0),
	ObjectHistory(999,274,-3.14159274,0),
	ObjectHistory(988,272,-3.29867244,0),
	ObjectHistory(978,270,-3.45575213,0),
	ObjectHistory(967,269,-3.61283183,0),
	ObjectHistory(957,267,-3.76991153,0),
	ObjectHistory(946,266,-3.92699122,0),
	ObjectHistory(936,264,-4.08407068,0),
	ObjectHistory(925,263,-4.24115038,0),
	ObjectHistory(914,262,-4.39823008,0),
	ObjectHistory(904,261,-4.55530977,0),
	ObjectHistory(893,260,-4.71238947,0),
	ObjectHistory(883,259,-4.86946917,0),
	ObjectHistory(872,258,-5.02654886,0),
	ObjectHistory(862,257,-5.18362856,0),
	ObjectHistory(851,257,-5.34070826,0),
	ObjectHistory(841,256,-5.49778795,0),
	ObjectHistory(830,255,-5.65486765,0),
	ObjectHistory(819,255,-5.81194735,0),
	ObjectHistory(809,255,-5.96902704,0),
	ObjectHistory(798,255,-6.12610674,0),
	ObjectHistory(788,254,0.00000000,0),
	ObjectHistory(777,254,-0.15707964,0),
	ObjectHistory(767,254,-0.31415927,0),
	ObjectHistory(756,255,-0.47123891,0),
	ObjectHistory(746,255,-0.62831855,0),
	ObjectHistory(735,255,-0.78539819,0),
	ObjectHistory(724,255,-0.94247782,0),
	ObjectHistory(714,256,-1.09955740,0),
	ObjectHistory(703,257,-1.25663698,0),
	ObjectHistory(693,257,-1.41371655,0),
	ObjectHistory(682,258,-1.57079613,0),
	ObjectHistory(671,259,-1.72787571,0),
	ObjectHistory(660,260,-1.72787571,1),
	ObjectHistory(649,261,-1.72787571,1),
	ObjectHistory(636,262,-1.72787571,1),
	ObjectHistory(623,264,-1.72787571,1),
	ObjectHistory(609,266,-1.72787571,1),
	ObjectHistory(595,268,-1.72787571,1),
	ObjectHistory(580,270,-1.72787571,1),
	ObjectHistory(563,272,-1.72787571,1),
	ObjectHistory(547,275,-1.72787571,0),
	ObjectHistory(532,278,-1.57079613,0),
	ObjectHistory(516,280,-1.41371655,0),
	ObjectHistory(500,283,-1.25663698,0),
	ObjectHistory(484,286,-1.09955740,0),
	ObjectHistory(468,289,-0.94247776,0),
	ObjectHistory(452,292,-0.78539813,0),
	ObjectHistory(437,295,-0.62831849,0),
	ObjectHistory(421,298,-0.47123885,0),
	ObjectHistory(405,301,-0.31415921,0),
	ObjectHistory(389,305,-0.15707958,0),
	ObjectHistory(374,308,0.00000000,0),
	ObjectHistory(357,312,0.15707964,0),
	ObjectHistory(342,315,0.31415927,0),
	ObjectHistory(327,319,0.47123891,0),
	ObjectHistory(310,323,0.62831855,0),
	ObjectHistory(294,327,0.78539819,0),
	ObjectHistory(279,331,0.94247782,0),
	ObjectHistory(263,335,1.09955740,0),
	ObjectHistory(247,339,1.25663698,0),
	ObjectHistory(232,343,1.25663698,1),
	ObjectHistory(217,347,1.25663698,1),
	ObjectHistory(204,351,1.25663698,1),
	ObjectHistory(190,354,1.25663698,1),
	ObjectHistory(177,358,1.25663698,1),
	ObjectHistory(166,361,1.25663698,1),
	ObjectHistory(154,365,1.25663698,1),
	ObjectHistory(143,368,1.25663698,1),
	ObjectHistory(133,371,1.25663698,1),
	ObjectHistory(124,374,1.25663698,1),
	ObjectHistory(114,377,1.25663698,1),
	ObjectHistory(107,380,1.25663698,1),
	ObjectHistory(99,382,1.25663698,1),
	ObjectHistory(92,385,1.25663698,1),
	ObjectHistory(86,387,1.25663698,1),
	ObjectHistory(80,390,1.25663698,1),
	ObjectHistory(75,392,1.25663698,1),
	ObjectHistory(73,393,1.25663698,1),
	ObjectHistory(69,395,1.25663698,1),
	ObjectHistory(66,397,1.25663698,1),
	ObjectHistory(63,398,1.25663698,1),
	ObjectHistory(61,400,1.25663698,1),
	ObjectHistory(60,401,1.25663698,1),
	ObjectHistory(59,402,1.25663698,1),
	ObjectHistory(59,404,1.25663698,1),
	ObjectHistory(60,405,1.25663698,1),
	ObjectHistory(61,406,1.25663698,1),
	ObjectHistory(63,407,1.25663698,1),
	ObjectHistory(66,407,1.25663698,1),
	ObjectHistory(69,408,1.25663698,1),
	ObjectHistory(73,408,1.25663698,1),
	ObjectHistory(77,409,1.25663698,1),
	ObjectHistory(82,409,1.25663698,1),
	ObjectHistory(88,409,1.25663698,1),
	ObjectHistory(94,409,1.25663698,1),
	ObjectHistory(101,409,1.25663698,1),
	ObjectHistory(109,409,1.25663698,1),
	ObjectHistory(117,409,1.25663698,1),
	ObjectHistory(127,408,1.25663698,1),
	ObjectHistory(136,407,1.25663698,1)
};

