# Version 1 Release RoadMap

We want to eventually have mostly stable binaries for the editor and game so that building upon that doesn't cause things to break or become incompatible too often. I also want to add some basic documentation for things

## Editor Stability improvements and bug fixes (Current)

I'm currently trying to make it so there's no potential crashes or breaking issues when updating from one version to another, or when adding new entities. I also want it to be performant throughout this project, preferably i can eventually create a scene with 2048 entities (Half life 2's maximum ammount) and still maintain 150+ FPS on a 1050 TI or similar card (which is what i own).

## Further game and engine development with a focus on some new features and organization

Need to catch up with some of the editor's quality of life features (an input screen, having options be saved, have a cvar for loading maps directly, having a menu for opening maps). I also want to do some much needed organization separating game code from engine code since right now it's all kind of a mess that'll get worse later, maybe also add some documentation along the way. And finally i want to add a few mechanics and add in new entities.

## Using materials and dealing with assets

The goal for this step is a material system for both the game and the editor, built from the textures folder: each texture gets paired with its `_normalmap` and `_specularmap` files when it has them. To show it off, this step ends with a small test scene with visual quality close to Source engine or Unreal Engine 2 games.

## Adding weapons and enemies. building a vertical slice prototype. 

In this phase i want to create a playable vertical slice of quake, specifically a demo that has the axe and the shotgun as usable weapons, and the Ogre and Knight as working enemies. All put together in a small demo level

## Bug testing and any last stability fixes

want to do a big round of testing to see if there are any massive flaws or other problems that require setting the release back and going through another round of changes

## Adding a logo and doing some marketing.

want to be able to have an extended showcase of what the engine can do as well as the existing mechanics of the game. Just more branding in general as well.

## Release

Put out the release alongisde the source code, it'll probably be windows only unless someone decides to add a way to build on linux/mac.