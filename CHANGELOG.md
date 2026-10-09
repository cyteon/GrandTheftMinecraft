# Changelog

## 1.5.1

- Fixed crashes around getting into vehicles: the mod now treats getting in as being in a vehicle (no Minecraft
  on-foot logic during GTA's animation), lets go of flight, gliding and ladders when you press the enter key, and only
  switches you to bare hands when you're holding something else instead of every frame.
- If GTA crashes, `gtm.log` now ends with a crash report (where it crashed, the call stack and what the mod was doing),
  next to Rockstar's own crash dump.

## 1.5.0

- **Golems built from blocks**, like in Minecraft: a T of four iron blocks with a carved pumpkin (or jack o'lantern)
  on top makes an iron golem; two snow blocks with a pumpkin make a **snow golem**, which throws snowballs at hostile
  mobs.
- **New mobs** (Spawn Eggs tab): pigs, cows, sheep and chickens that wander about and panic when hurt; spiders that
  hunt at night (leaping at their prey) and leave you alone by day unless hit; endermen that wander, carry your
  blocks off and put them down elsewhere, teleport away when hurt and come back for whoever hit them. The animals and
  spiders ride GTA's own animals (hen, pig, cow, boar, coyote), so they're the right size to shoot and run over, and
  their legs move with their walk like Minecraft's. Spiders climb walls to get at prey above them.
- **Shears**: right-click a sheep to shear it; its wool grows back after a minute or two.
- **Falling blocks**: sand, gravel, concrete powder and anvils fall when nothing holds them up. Anvils hurt whoever
  they land on and dent cars.
- **Redstone power** (direct, no wire yet): levers, buttons, pressure plates, redstone blocks and redstone torches
  power the blocks touching them, and a lever or button powers the block it's mounted on too. Powered doors,
  trapdoors and fence gates open (iron doors and trapdoors only this way, like in Minecraft), redstone lamps light up,
  TNT ignites and note blocks play.
- **Exact hitboxes**: aiming, placing and the selection outline use each block's real shape (a stair's steps, a
  slab's half, a fence's post and rails, a pressure plate's sliver) instead of a full block, so you can click past or
  onto them like in Minecraft. Arrows stick where they really hit.
- Fixed occasional crashes: every GTA object the mod creates now counts against one budget (`MaxModObjects`, 1100)
  so GTA's fixed object pools can't run out, at most 6 arrows stick in one person or car, and every collision probe's
  result is read so its slot is freed.
- Fixed: typing on a sign no longer opens the inventory, switches hotbar slots or toggles Minecraft mode.
- Fixed: turning Minecraft mode off with a chest open left the chest open (and E showed it again).
- Fixed: Steve's held item in third person and his gliding pose (body part names got mixed up in 1.4.0).
- Fixed: sign text could lose letters when several signs were in view.
- Pressure plates and campfires also react while you're driving.

## 1.4.0

- **Shaped blocks** (about 270 new): slabs (top, bottom, double), stairs (with Minecraft's corner shapes), walls,
  fences and fence gates that connect to their neighbours, glass panes and iron bars, carpets, torches (on the floor
  or on walls), lanterns (standing or hanging), flowers, saplings, grass and other plants, cobwebs, ladders, doors
  (two blocks tall, double doors pair up) and trapdoors. Right-click opens doors, trapdoors and gates (iron ones
  need redstone, like in Minecraft). Torches, plants, carpets and ladders pop off when what holds them is broken.
- **133 more full blocks**: all 16 glazed terracottas, copper in every stage (cut, chiseled, grates), wood and
  stripped logs for every tree, chiseled and cracked stone variants, quartz pillar, smooth sandstone, coral blocks,
  froglights, mushroom blocks, sculk, deepslate ores, barrel, loom, fletching / smithing / cartography tables,
  chiseled bookshelf, lodestone, respawn anchor, spawner, crafter and more.
- **Mob heads**: skeleton skull, zombie head, creeper head and player head.
- **Furniture and redstone blocks** (91 new): beds in all 16 colours (right-click at night to sleep until morning),
  chests, trapped chests and ender chests (right-click to open: a lid that lifts and 27 slots that keep what you put
  in), signs in every wood (standing or on walls; type your text with GTA's keyboard, '|' starts a new line, and it
  shows on the sign), banners in all 16 colours, buttons and levers (they press and flip), pressure plates (they go
  down when people or cars are on them), rails (they join up and curve at corners), anvils, enchanting table, brewing
  stand, cauldron, campfires (they glow and burn whoever stands in them) and flower pots.
- **Ladders can be climbed**: walk or jump into one to climb, let go to slide down, crouch to hold on.
- Arrows, ender pearls and wither skulls fly through torches, plants, carpets, signs and the like instead of
  sticking in the air around them.
- Block textures are compressed on the GPU (DXT, lossless for Minecraft's pixel art): the block pack uses about a
  third of the memory it did.

## 1.3.0

- **Tools in every tier**: wooden, stone, copper, iron, golden, diamond and netherite swords, axes, pickaxes,
  shovels and hoes, each hitting as hard as in Minecraft (a netherite axe does 10, a wooden sword 4).
- **Blocks with a front** face you when you place them: furnace, blast furnace, smoker, beehive, carved pumpkin,
  jack o'lantern, dispenser, dropper and the crafting table. Skulls turn towards you in Minecraft's 16 steps, and
  only their head is solid.
- Swords no longer break blocks (Minecraft creative).
- Flying and gliding with an elytra keep going while the inventory is open.
- Skeletons (and you) hold bows the right way round.
- Arrows and cars: an arrow through a car window now smashes that window and hits whoever sits behind it. It used
  to smash the far window, or count the glass as bodywork, and miss the people inside (GTA's cars have no glass
  collision, and a fast arrow can be inside the car before anything registers a hit). Doors, bonnet and roof catch
  the arrow, and an arrow crossing the cabin leaves through the far window or sticks in the far door.
- **155 more blocks**: every wood type's logs and planks, stone variants (granite, diorite, andesite, deepslate,
  tuff, calcite, mossy and cracked bricks...), nether and end blocks, ores, raw metal blocks, copper, netherite,
  amethyst, leaves, podzol, mycelium, mud, clay, ice, glowing blocks (sea lantern, shroomlight, magma, crying
  obsidian), redstone lamp, target, note block, slime and honey blocks, and all 16 colours of terracotta, stained
  glass and concrete powder.

## 1.2.0

- **The Wither**, summoned like in Minecraft: build a T of four soul sand (or soul soil) and put three wither
  skeleton skulls on top (new blocks: Soul Sand and Soul Soil in Building Blocks, Wither Skeleton Skull in the new
  Functional Blocks tab). It's a flying three-headed boss with Minecraft's boss bar. It charges up and blows up when
  it spawns, hovers over its targets and fires exploding wither skulls from all three heads at GTA's people, cops and
  cars (skulls break blocks too). Below half health it drops to its target's height and fires faster. Cops shoot it,
  iron golems go after it, and it goes out with a huge explosion.
- **Minecraft's creative tabs**: Building, Colored, Natural, Functional and Redstone Blocks along the top; Tools &
  Utilities, Combat, Food & Drinks, Ingredients and Spawn Eggs along the bottom, with Minecraft's tab icons. Blocks
  sit in the same tabs as in Minecraft (some in more than one).
- **Food and ingredients** to hold: apple, golden apple, bread, steak, cooked porkchop, cookie, coal, iron and gold
  ingots, diamond, emerald, lapis lazuli, redstone dust, stick, gunpowder.

## 1.1.0

- **Mobs that fight GTA's people** (new Spawn Eggs tab): zombies chase and hit pedestrians, skeletons keep their
  distance and shoot arrows, creepers hiss, flash and explode, iron golems fight on your side against monsters, cops
  and gangs. Cops shoot hostile mobs. Hurt and death sounds, death poof.
- **Play as Steve** in third person (or your own skin with `SkinFile=`): animated by GTA's walk, run, jump and ragdoll
  animations, head follows your camera, holds your item, swings and aims.
- **Elytra**: right-click to wear it, press Space while falling to glide with Minecraft's flight physics
  (`ElytraSpeed=` to tune, 1.6x by default). Wings on Steve's back, folded or spread.
- **Firework rockets**: boost while gliding, or launch one to burst into coloured sparks.
- **Arrows** stick in people (the body part they hit, with a wound and knock-back) and in cars (no bullet holes),
  smash car windows and hit the people inside, burst tyres, and fly through shop glass. They leave from your head
  towards the crosshair.
- No GTA drive-by guns in Minecraft mode.
- Fixes: the held item, block chips, primed TNT and arrows no longer show black before the block pack is textured;
  the first launch usually no longer needs a restart.

## 1.0.0

- Minecraft creative mode in GTA V story mode: hotbar, creative inventory, Minecraft font and sounds.
- 69 blocks as real GTA objects (lit, shadowed, solid), saved between sessions; cars get pushed out of the way.
- TNT, ender pearls, flint and steel, diamond sword, bow and crossbow, creative flight.
- Built on first launch from your own Minecraft 1.21.11 or Mojang's servers: no Minecraft files in the download.
