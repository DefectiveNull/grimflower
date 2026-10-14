#include "player.h"
#include "settings.h"
#include "gamecommands.h"
#include "volumes.h"
#include "raymath.h"

#include <cmath>

// Quake 1 movement values. World units are Quake units
static const Vector3 PLAYER_MINS = { -16.0f, -24.0f, -16.0f };  // Quake's hull, with y up
static const Vector3 PLAYER_MAXS = { 16.0f, 32.0f, 16.0f };
static const float VIEW_HEIGHT = 22.0f;                         // view_ofs
static const float MAX_SPEED = 192.0f;                          // Slower than sv_maxspeed 320, as it was tuned
static const float ACCELERATE = 10.0f;                         // sv_accelerate 10
static const float AIR_SPEED_CAP = 30.0f;                       // In SV_AirAccelerate
static const float FRICTION = 4.0f;                            // sv_friction 4
static const float STOP_SPEED = 100.0f;                         // sv_stopspeed
static const float GRAVITY = 800.0f;                            // sv_gravity
static const float JUMP_SPEED = 270.0f;                         // In PlayerJump
static const float STEP_SIZE = 18.0f;                           // STEPSIZE
static const float STOP_EPSILON = 0.1f;                         // STOP_EPSILON
static const float MIN_FLOOR_NORMAL = 0.7f;                    // Steeper than this is a wall

static const float SENSITIVITY_SCALE = 0.001f;                 // Radians per pixel at sensitivity 1
static const float MAX_PITCH = 89.0f*DEG2RAD;
static const float KILL_HEIGHT = -1600.0f;                      // Respawn when falling below this

// Crouching halves the hull's height, keeping the feet where they are, like Half-Life's duck hull
static const Vector3 DUCK_MAXS = { 16.0f, 4.0f, 16.0f };
static const float DUCK_VIEW_HEIGHT = 0.0f;
static const float DUCK_SPEED_SCALE = 0.333f;                  // PLAYER_DUCKING_MULTIPLIER

static const float SWIM_SPEED_SCALE = 0.8f;                    // Half-Life swims at 80% of the walking speed
static const float SINK_SPEED = 60.0f;                          // Drifting down with no input
static const float CLIMB_SPEED = 120.0f;                        // MAX_CLIMB_SPEED 200 at Half-Life's speeds, scaled like MAX_SPEED
static const float CLIMB_DOWN_PITCH = -30.0f*DEG2RAD;          // Looking further down than this, forward climbs down
static const float WATER_JUMP_REACH = 40.0f;                   // Highest ledge above the waist a water jump climbs onto
static const float WATER_JUMP_SPEED = 96.0f;                   // Towards the ledge while jumping out
static const float WATER_JUMP_TIME = 1.0f;                     // Longest a water jump lasts, if it never lands

static const int BLOCKED_FLOOR = 1;
static const int BLOCKED_STEP = 2;

void InitPlayer(Player &player, Vector3 spawn, float yaw)
{
    player = {};
    player.position = spawn;
    player.spawn = spawn;
    player.yaw = yaw;
    player.spawnYaw = yaw;
    player.jumpReleased = true;
}

// Removes the part of the velocity going into a plane
static Vector3 ClipVelocity(Vector3 in, Vector3 normal, float overbounce)
{
    float backoff = Vector3DotProduct(in, normal)*overbounce;
    Vector3 out = Vector3Subtract(in, Vector3Scale(normal, backoff));

    if (fabsf(out.x) < STOP_EPSILON) out.x = 0.0f;
    if (fabsf(out.y) < STOP_EPSILON) out.y = 0.0f;
    if (fabsf(out.z) < STOP_EPSILON) out.z = 0.0f;

    return out;
}

static Vector3 HullMaxs(const Player &player)
{
    return player.ducked ? DUCK_MAXS : PLAYER_MAXS;
}

BoundingBox GetPlayerBox(const Player &player)
{
    return { Vector3Add(player.position, PLAYER_MINS), Vector3Add(player.position, HullMaxs(player)) };
}

// Moves the player by a fixed offset, stopping at the first thing hit
static Trace PushMove(Player &player, const World &world, Vector3 push)
{
    Trace trace = TraceBox(world, player.position, Vector3Add(player.position, push), PLAYER_MINS, HullMaxs(player));
    player.position = trace.endPos;
    return trace;
}

// Moves along the velocity, sliding along up to 4 surfaces (SV_FlyMove)
static int FlyMove(Player &player, const World &world, float time)
{
    const int MAX_BUMPS = 4;
    const int MAX_PLANES = 5;

    Vector3 planes[MAX_PLANES];
    int numPlanes = 0;
    int blocked = 0;

    Vector3 primalVelocity = player.velocity;
    Vector3 originalVelocity = player.velocity;
    float timeLeft = time;

    for (int bump = 0; bump < MAX_BUMPS; bump++)
    {
        if (Vector3LengthSqr(player.velocity) == 0.0f) break;

        Vector3 end = Vector3Add(player.position, Vector3Scale(player.velocity, timeLeft));
        Trace trace = TraceBox(world, player.position, end, PLAYER_MINS, HullMaxs(player));

        if (trace.fraction > 0.0f)
        {
            // Covered some distance, so the old clip planes no longer apply
            player.position = trace.endPos;
            originalVelocity = player.velocity;
            numPlanes = 0;
        }

        if (trace.fraction == 1.0f) break;

        if (trace.normal.y > MIN_FLOOR_NORMAL)
        {
            blocked |= BLOCKED_FLOOR;
            player.onGround = true;
        }
        if (fabsf(trace.normal.y) < 0.001f) blocked |= BLOCKED_STEP;  // Vertical wall, might be a step

        timeLeft -= timeLeft*trace.fraction;

        if (numPlanes >= MAX_PLANES)
        {
            player.velocity = { 0.0f, 0.0f, 0.0f };
            break;
        }
        planes[numPlanes++] = trace.normal;

        // Find a velocity that slides along all the planes hit so far
        int i, j;
        Vector3 newVelocity = { 0 };
        for (i = 0; i < numPlanes; i++)
        {
            newVelocity = ClipVelocity(originalVelocity, planes[i], 1.0f);
            for (j = 0; j < numPlanes; j++)
            {
                if (j != i && Vector3DotProduct(newVelocity, planes[j]) < 0.0f) break;
            }
            if (j == numPlanes) break;
        }

        if (i != numPlanes)
        {
            player.velocity = newVelocity;
        }
        else
        {
            // Stuck in a corner: slide along the crease between two planes, or stop
            if (numPlanes != 2)
            {
                player.velocity = { 0.0f, 0.0f, 0.0f };
                break;
            }
            Vector3 dir = Vector3CrossProduct(planes[0], planes[1]);
            player.velocity = Vector3Scale(dir, Vector3DotProduct(dir, player.velocity));
        }

        // Don't bounce back the way we came, to avoid jittering in corners
        if (Vector3DotProduct(player.velocity, primalVelocity) <= 0.0f)
        {
            player.velocity = { 0.0f, 0.0f, 0.0f };
            break;
        }
    }

    return blocked;
}

// Moves the player, stepping up onto ledges no higher than STEP_SIZE (SV_WalkMove)
static void WalkMove(Player &player, const World &world, float dt)
{
    bool wasOnGround = player.onGround;
    player.onGround = false;

    Vector3 oldPosition = player.position;
    Vector3 oldVelocity = player.velocity;

    int blocked = FlyMove(player, world, dt);

    if (!(blocked & BLOCKED_STEP)) return;  // Didn't hit a wall
    if (!wasOnGround) return;               // Don't climb steps while in the air

    Vector3 noStepPosition = player.position;
    Vector3 noStepVelocity = player.velocity;
    bool noStepOnGround = player.onGround;

    // Retry the move from STEP_SIZE higher, then drop back down
    player.position = oldPosition;
    PushMove(player, world, { 0.0f, STEP_SIZE, 0.0f });

    player.velocity = { oldVelocity.x, 0.0f, oldVelocity.z };
    FlyMove(player, world, dt);

    Trace down = PushMove(player, world, { 0.0f, -STEP_SIZE + oldVelocity.y*dt, 0.0f });
    if (down.normal.y > MIN_FLOOR_NORMAL)
    {
        player.onGround = true;
    }
    else
    {
        // Didn't land on solid ground, so use the move without the step
        player.position = noStepPosition;
        player.velocity = noStepVelocity;
        player.onGround = noStepOnGround;
    }
}

// Slows the player down. On the ground only horizontal speed is affected; when flying, all of it is
static void ApplyFriction(Player &player, float dt, bool includeVertical)
{
    float verticalSq = includeVertical ? player.velocity.y*player.velocity.y : 0.0f;
    float speed = sqrtf(player.velocity.x*player.velocity.x + verticalSq + player.velocity.z*player.velocity.z);
    if (speed == 0.0f) return;

    // Below STOP_SPEED, friction acts as if moving at STOP_SPEED, so the player comes to a full stop quickly
    float control = (speed < STOP_SPEED) ? STOP_SPEED : speed;
    float newSpeed = fmaxf(0.0f, speed - dt*control*FRICTION);

    player.velocity.x *= newSpeed/speed;
    player.velocity.z *= newSpeed/speed;
    if (includeVertical) player.velocity.y *= newSpeed/speed;
}

static void Accelerate(Player &player, Vector3 wishDir, float wishSpeed, float dt)
{
    float currentSpeed = Vector3DotProduct(player.velocity, wishDir);
    float addSpeed = wishSpeed - currentSpeed;
    if (addSpeed <= 0.0f) return;

    float accelSpeed = fminf(ACCELERATE*dt*wishSpeed, addSpeed);
    player.velocity = Vector3Add(player.velocity, Vector3Scale(wishDir, accelSpeed));
}

// Same as Accelerate, but only caps the speed gained along wishDir, which is what allows air strafing
static void AirAccelerate(Player &player, Vector3 wishDir, float wishSpeed, float dt)
{
    float cappedSpeed = fminf(wishSpeed, AIR_SPEED_CAP);
    float currentSpeed = Vector3DotProduct(player.velocity, wishDir);
    float addSpeed = cappedSpeed - currentSpeed;
    if (addSpeed <= 0.0f) return;

    float accelSpeed = fminf(ACCELERATE*wishSpeed*dt, addSpeed);
    player.velocity = Vector3Add(player.velocity, Vector3Scale(wishDir, accelSpeed));
}

static float ViewHeight(const Player &player)
{
    return player.ducked ? DUCK_VIEW_HEIGHT : VIEW_HEIGHT;
}

// How deep in water the player is, checking the feet, the waist (the origin) and the eyes (PM_CheckWater)
static int WaterLevel(const Player &player, const Volumes &volumes)
{
    Vector3 feet = { player.position.x, player.position.y + PLAYER_MINS.y + 1.0f, player.position.z };
    if (!(PointContents(volumes, feet) & CONTENTS_WATER)) return 0;
    if (!(PointContents(volumes, player.position) & CONTENTS_WATER)) return 1;

    Vector3 eyes = { player.position.x, player.position.y + ViewHeight(player), player.position.z };
    return (PointContents(volumes, eyes) & CONTENTS_WATER) ? 3 : 2;
}

// Holding duck crouches. Letting go stands back up, but only once there's room above
static void UpdateDuck(Player &player, const World &world, bool canDuck)
{
    if (inputButtons.duck && canDuck)
    {
        player.ducked = true;
        return;
    }
    if (!player.ducked) return;

    // Checked from a hair higher: standing exactly on the floor counts as being inside it otherwise,
    // and the player could never stand up without jumping first
    Vector3 checkFrom = Vector3Add(player.position, { 0.0f, 0.25f, 0.0f });
    Trace room = TraceBox(world, checkFrom, checkFrom, PLAYER_MINS, PLAYER_MAXS);
    if (!room.startSolid) player.ducked = false;
}

// On a ladder: no gravity, forward climbs (down while looking down), jump lets go (PM_LadderMove)
static void LadderMove(Player &player, const World &world, float dt)
{
    Vector3 forward = { -sinf(player.yaw), 0.0f, -cosf(player.yaw) };
    Vector3 right = { cosf(player.yaw), 0.0f, -sinf(player.yaw) };

    if (!inputButtons.jump)
    {
        player.jumpReleased = true;
    }
    else if (player.jumpReleased)
    {
        // Push off backwards, and don't grab the ladder again straight away
        player.jumpReleased = false;
        player.velocity = Vector3Add(Vector3Scale(forward, -MAX_SPEED*0.5f), { 0.0f, JUMP_SPEED*0.5f, 0.0f });
        player.ladderCooldown = 0.3f;
        player.onGround = false;
        FlyMove(player, world, dt);
        return;
    }

    float forwardMove = (float)inputButtons.forward - (float)inputButtons.back;
    float sideMove = (float)inputButtons.moveRight - (float)inputButtons.moveLeft;
    float climb = (player.pitch < CLIMB_DOWN_PITCH) ? -forwardMove : forwardMove;

    // Moving towards the ladder too, so climbing off the top carries the player onto the ledge
    Vector3 across = Vector3Add(Vector3Scale(forward, forwardMove), Vector3Scale(right, sideMove));
    player.velocity = Vector3Scale(across, CLIMB_SPEED*0.5f);
    player.velocity.y = climb*CLIMB_SPEED;

    player.onGround = false;
    FlyMove(player, world, dt);
}

// Waist deep or more: swim where you look, jump up, duck down, and slowly sink without input (PM_WaterMove)
static void WaterMove(Player &player, const World &world, float dt)
{
    Vector3 look = {
        -sinf(player.yaw)*cosf(player.pitch),
        sinf(player.pitch),
        -cosf(player.yaw)*cosf(player.pitch)
    };
    Vector3 right = { cosf(player.yaw), 0.0f, -sinf(player.yaw) };
    float forwardMove = (float)inputButtons.forward - (float)inputButtons.back;
    float sideMove = (float)inputButtons.moveRight - (float)inputButtons.moveLeft;
    float upMove = (float)(inputButtons.jump || inputButtons.moveUp) - (float)(inputButtons.duck || inputButtons.moveDown);

    Vector3 wish = Vector3Add(Vector3Scale(look, forwardMove), Vector3Scale(right, sideMove));
    wish.y += upMove;
    Vector3 wishDir = Vector3Normalize(wish);
    float wishSpeed = MAX_SPEED*SWIM_SPEED_SCALE;
    if (Vector3LengthSqr(wishDir) == 0.0f)
    {
        wishDir = { 0.0f, -1.0f, 0.0f };
        wishSpeed = SINK_SPEED;
    }

    ApplyFriction(player, dt, true);
    Accelerate(player, wishDir, wishSpeed, dt);

    // With the head above water, jump hops up, so the player can climb out onto a ledge
    if (player.waterLevel == 2 && inputButtons.jump) player.velocity.y = fmaxf(player.velocity.y, JUMP_SPEED*0.8f);

    player.onGround = false;
    FlyMove(player, world, dt);
}

// Swimming forward into a wall near the surface, with a ledge in reach above it, jumps out of the water
// onto the ledge (PM_CheckWaterJump). Unlike Quake's fixed jump speed, it jumps just high enough for the
// ledge it found, so a water surface a little below the ledge works too
static void CheckWaterJump(Player &player, const World &world)
{
    if (player.waterJumpTime > 0.0f || player.waterLevel == 0 || player.waterLevel == 3 || player.onGround) return;
    if (!inputButtons.forward || inputButtons.back) return;

    // A wall right in front, that the player is facing
    Vector3 forward = { -sinf(player.yaw), 0.0f, -cosf(player.yaw) };
    Vector3 maxs = HullMaxs(player);
    Trace wall = TraceBox(world, player.position, Vector3Add(player.position, Vector3Scale(forward, 10.0f)), PLAYER_MINS, maxs);
    if (wall.fraction == 1.0f || wall.startSolid || fabsf(wall.normal.y) > 0.1f) return;
    if (Vector3DotProduct(forward, wall.normal) > -0.5f) return;

    // Room to rise up above the ledge, and to move over it from there
    Trace up = TraceBox(world, player.position, Vector3Add(player.position, { 0.0f, WATER_JUMP_REACH, 0.0f }), PLAYER_MINS, maxs);
    Trace over = TraceBox(world, up.endPos, Vector3Add(up.endPos, Vector3Scale(forward, 10.0f)), PLAYER_MINS, maxs);
    if (over.fraction < 1.0f || over.startSolid) return;

    // The ledge's top, which has to be above where the player is now, and flat enough to stand on
    Trace down = TraceBox(world, over.endPos, { over.endPos.x, player.position.y, over.endPos.z }, PLAYER_MINS, maxs);
    if (down.fraction == 1.0f || down.startSolid || down.normal.y < MIN_FLOOR_NORMAL) return;
    float rise = down.endPos.y - player.position.y;
    if (rise <= 0.0f) return;

    // Just fast enough to clear it, with a little to spare to move over the edge
    player.velocity = Vector3Scale(forward, WATER_JUMP_SPEED);
    player.velocity.y = sqrtf(2.0f*GRAVITY*(rise + 5.0f));
    player.waterJumpDir = forward;
    player.waterJumpTime = WATER_JUMP_TIME;
}

// Jumping out of the water: carried towards the ledge under gravity, without control, until landing
static void WaterJumpMove(Player &player, const World &world, float dt)
{
    player.waterJumpTime -= dt;
    player.velocity.x = player.waterJumpDir.x*WATER_JUMP_SPEED;
    player.velocity.z = player.waterJumpDir.z*WATER_JUMP_SPEED;
    player.velocity.y -= GRAVITY*dt;
    WalkMove(player, world, dt);
    if (player.onGround) player.waterJumpTime = 0.0f;
}

void UpdatePlayer(Player &player, const World &world, const Volumes &volumes, float dt)
{
    // Capturing the mouse moves the cursor to the middle of the window, and that jump shows up as mouse
    // movement for a frame or two. Skip those, or the view spins away from where it was facing
    bool cursorHidden = IsCursorHidden();
    if (cursorHidden && !player.cursorWasHidden) player.mouseSettleFrames = 2;
    player.cursorWasHidden = cursorHidden;

    // Mouse look
    if (cursorHidden && player.mouseSettleFrames > 0)
    {
        player.mouseSettleFrames--;
    }
    else if (cursorHidden)
    {
        Vector2 mouseDelta = GetMouseDelta();
        float lookSpeed = sensitivity.value*SENSITIVITY_SCALE;
        float pitchDirection = (m_invert.value != 0.0f) ? -1.0f : 1.0f;
        player.yaw -= mouseDelta.x*lookSpeed;
        player.pitch = Clamp(player.pitch - pitchDirection*mouseDelta.y*lookSpeed, -MAX_PITCH, MAX_PITCH);
    }

    // Buttons come from key binds running the + and - commands, see gamecommands.cpp
    if (inputButtons.toggleFly)
    {
        inputButtons.toggleFly = false;
        player.flying = !player.flying;
        player.onGround = false;
        player.jumpReleased = false;  // Jump held while flying up shouldn't jump right after landing
    }

    if (player.flying)
    {
        // Move along the full view direction, jump/moveup and movedown for straight up and down
        Vector3 look = {
            -sinf(player.yaw)*cosf(player.pitch),
            sinf(player.pitch),
            -cosf(player.yaw)*cosf(player.pitch)
        };
        Vector3 right = { cosf(player.yaw), 0.0f, -sinf(player.yaw) };
        float forwardMove = (float)inputButtons.forward - (float)inputButtons.back;
        float sideMove = (float)inputButtons.moveRight - (float)inputButtons.moveLeft;
        float upMove = (float)(inputButtons.jump || inputButtons.moveUp) - (float)(inputButtons.moveDown || inputButtons.duck);

        Vector3 wish = Vector3Add(Vector3Scale(look, forwardMove), Vector3Scale(right, sideMove));
        wish.y += upMove;
        Vector3 wishDir = Vector3Normalize(wish);
        float wishSpeed = (Vector3LengthSqr(wishDir) > 0.0f) ? MAX_SPEED : 0.0f;

        UpdateDuck(player, world, false);  // Stands back up if crouching when fly was turned on
        ApplyFriction(player, dt, true);
        Accelerate(player, wishDir, wishSpeed, dt);
        FlyMove(player, world, dt);
        player.onGround = false;  // FlyMove flags floors it touches, but a flying player never stands
        return;
    }

    // What the player is in: water, and whether they're touching a ladder
    player.waterLevel = WaterLevel(player, volumes);
    player.ladderCooldown = fmaxf(player.ladderCooldown - dt, 0.0f);
    player.onLadder = player.ladderCooldown <= 0.0f && (BoxContents(volumes, GetPlayerBox(player)) & CONTENTS_LADDER);

    // Duck means swim down in water, so only crouch out of it
    UpdateDuck(player, world, player.waterLevel < 2 && !player.onLadder);

    if (player.onLadder)
    {
        LadderMove(player, world, dt);
        return;
    }
    CheckWaterJump(player, world);
    if (player.waterJumpTime > 0.0f)
    {
        WaterJumpMove(player, world, dt);
        return;
    }
    if (player.waterLevel >= 2)
    {
        WaterMove(player, world, dt);
        return;
    }

    // Jumping
    if (!inputButtons.jump)
    {
        player.jumpReleased = true;
    }
    else if (player.onGround && player.jumpReleased)
    {
        player.jumpReleased = false;
        player.onGround = false;
        player.velocity.y += JUMP_SPEED;
    }

    // Movement input, on the horizontal plane only
    Vector3 forward = { -sinf(player.yaw), 0.0f, -cosf(player.yaw) };
    Vector3 right = { cosf(player.yaw), 0.0f, -sinf(player.yaw) };
    float forwardMove = (float)inputButtons.forward - (float)inputButtons.back;
    float sideMove = (float)inputButtons.moveRight - (float)inputButtons.moveLeft;

    Vector3 wishDir = Vector3Normalize(Vector3Add(Vector3Scale(forward, forwardMove), Vector3Scale(right, sideMove)));
    float wishSpeed = (Vector3LengthSqr(wishDir) > 0.0f) ? MAX_SPEED : 0.0f;
    if (player.ducked) wishSpeed *= DUCK_SPEED_SCALE;

    if (player.onGround)
    {
        ApplyFriction(player, dt, false);
        Accelerate(player, wishDir, wishSpeed, dt);
    }
    else
    {
        AirAccelerate(player, wishDir, wishSpeed, dt);
    }

    player.velocity.y -= GRAVITY*dt;
    WalkMove(player, world, dt);

    if (player.position.y < KILL_HEIGHT) InitPlayer(player, player.spawn, player.spawnYaw);
}

Camera GetPlayerCamera(const Player &player)
{
    Vector3 look = {
        -sinf(player.yaw)*cosf(player.pitch),
        sinf(player.pitch),
        -cosf(player.yaw)*cosf(player.pitch)
    };

    Camera camera = { 0 };
    camera.position = Vector3Add(player.position, { 0.0f, ViewHeight(player), 0.0f });
    camera.target = Vector3Add(camera.position, look);
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    return camera;
}
