# PathKit @whiteboard/pathkit-wasm Changelog

Base on `pathkit-wasm/1.0.0`

## [1.0.16] 2024-04-10

 - add `isEmpty` to SkPath.

## [1.0.15] 2024-03-06

 - add `_toAATrianglesBuffer` to SkPath.

## [1.0.14] 2024-02-19

 - add `isHadCurve` to SkPath.

## [1.0.13] 2024-01-22

 - add `makeAsWinding` to SkPath.

## [1.0.12] 2023-08-25

 - remove `toTrianglesBuffer` method. merge to `toTriangles` method.
 - use shared memory of `toContours` to improve efficiency.

## [1.0.10] 2023-08-24

 - add `toTrianglesBuffer` method to SkPath, shared memory of wasm to improve efficiency.

## [1.0.9] 2023-08-18

 - fix `toContours` memory out of bound.

## [1.0.8] 2023-08-18

 - add `toContours` method to SkPath.

## [1.0.7] 2023-08-14

 - add `toTriangles` method to SkPath.

## [1.0.6] 2023-08-14

 - add `getLength` method to SkPath.

## [1.0.5] 2023-07-11

 - add `contains` method to SkPath.

## [1.0.4] 2023-06-20

 - add `getGenerationID` method to SkPath.
 - add `roundRect` method to SkPath.

## [1.0.2] 2023-06-19

### Changed

 - add `toNonConicCmds()` method to SkPath, it is like `toCmds` but it would convert conic to 2^1=2 quads.
 - add `reset` and `rewind` method to SkPath.
 - add `getLastPoint` method to SkPath.
