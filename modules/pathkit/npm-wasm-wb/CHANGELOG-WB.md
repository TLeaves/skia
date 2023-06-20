# PathKit @whiteboard/pathkit-wasm Changelog

Base on `pathkit-wasm/1.0.0`

## [1.0.4] 2023-06-20

 - add `getGenerationID` method to SkPath.
 - add `roundRect` method to SkPath.

## [1.0.2] 2023-06-19

### Changed

 - add `toNonConicCmds()` method to SkPath, it is like `toCmds` but it would convert conic to 2^1=2 quads.
 - add `reset` and `rewind` method to SkPath.
 - add `getLastPoint` method to SkPath.
