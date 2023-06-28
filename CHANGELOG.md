# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- Reflect in Storage capabilities interface that basic Storage devices do not support multiscale.

## [0.1.3](https://github.com/acquire-project/acquire-driver-common/compare/v0.1.2...v0.1.3) - 2023-06-27

### Changed

- Reflect a change in core-libs: `StorageProperties::chunking::max_bytes_per_chunk` is now a `uint64_t` (was
  a `uint32_t`).

### Added

- Can optionally link against the DLL version of the MSVC runtime. 

## [0.1.2](https://github.com/acquire-project/acquire-driver-common/compare/v0.1.1...v0.1.2) - 2023-05-25

### Added

- Nightly releases.
- Implements `get_meta` and `reserve_image_shape` for Raw, Tiff, and Side-by-Side Tiff with Json devices.

## 0.1.1 - 2023-05-11
