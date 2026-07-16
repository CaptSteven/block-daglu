# Third-party dependencies

## SuiteSparse

BlockDAGLU uses SuiteSparse KLU, BTF, AMD, COLAMD and SuiteSparseConfig through CMake/vcpkg.

- Upstream: https://github.com/DrTimothyAldenDavis/SuiteSparse
- Dependency snapshot used by CI: vcpkg `2024.05.24`
- Licenses vary by SuiteSparse component; retain the license files installed with the package when distributing binaries.

The public repository does not redistribute prebuilt SuiteSparse DLLs.

## SparseMatrixLUTestTool and Final15

The competition toolchain and Final15 dataset are not part of the public repository. They must be obtained under their original authorization and installed through the SHA-256-verifying scripts in `scripts/`.
