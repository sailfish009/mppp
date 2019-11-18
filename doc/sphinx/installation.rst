.. _installation:

Installation
============

Introduction
------------

mp++ is written in modern C++, and it requires a compiler able to understand
at least C++11 (mp++ will also use some features from C++14 and C++17,
if supported by the compiler). mp++ is known to run on the following setups:

* GCC 4.8 and later versions on GNU/Linux 32/64-bit,
* Clang 3.8 and later versions on GNU/Linux 64-bit,
* MSVC 2015 and later versions on Windows 32/64-bit,
* Clang 4 and later versions on Windows 32/64-bit
  (with the ``clang-cl`` driver for MSVC),
* MinGW GCC 8 on Windows 64-bit,
* Clang on OSX 64-bit (Xcode 6.4 and later),
* Intel compiler ICC 17 on GNU/Linux 64-bit (tested only occasionally).

mp++ has been written, tested and benchmarked on x86 processors, but it does
not contain any architecture-specific code
and thus it should run on any architecture supported by GMP
(as long as a decent C++11 compiler is available).
mp++ does use compiler-specific functionality
(e.g., 128-bit `integers <https://gcc.gnu.org/onlinedocs/gcc/_005f_005fint128.html>`__
or intrinsics on 64-bit architectures) for the implementation of
fast integer arithmetics. If such functionality is not available, mp++
will fall back to GMP's ``mpn_`` functions.

mp++ has the following dependencies:

* the `GMP <https://gmplib.org/>`__ library, **mandatory** (GMP 5 and later versions are supported,
  the `MPIR <http://mpir.org/>`__ fork of GMP can also be used);
* the `GNU MPFR <https://www.mpfr.org>`__ multiprecision floating-point library, *optional*, used in the implementation
  of the :cpp:class:`~mppp::real` class and for providing support
  for the ``long double`` type (MPFR 3 or a later version is required);
* the `quadmath library <https://gcc.gnu.org/onlinedocs/libquadmath/>`__ from GCC, *optional*, used
  in the implementation of the :cpp:class:`~mppp::real128` class
  (typically, the quadmath library is part of GCC and it does not need to
  be installed separately);
* the `Boost <https://www.boost.org/>`__ and `FLINT <http://flintlib.org/>`__ libraries, *optional*, currently used
  only in the benchmarking suite.

Additionally, `CMake <https://cmake.org/>`__ is the build system used by mp++ and it must also be available when
installing from source (the minimum required version is 3.3).

Installation from source
------------------------

Source releases of mp++ can be downloaded from `github <https://github.com/bluescarni/mppp/releases>`__. Once in the source tree
of mp++, you can use ``cmake`` to configure the build to your liking
(e.g., enabling optional features, customizing the installation
path, etc.). The available configuration options are:

* ``MPPP_WITH_MPFR``: enable features relying on the GNU
  MPFR library (off by default),
* ``MPPP_WITH_QUADMATH``: enable features relying on the
  quadmath library (off by default),
* ``MPPP_BUILD_TESTS``: build the test suite (off by default),
* ``MPPP_BUILD_BENCHMARKS``: build the benchmarking suite (off by default),
* ``MPPP_BUILD_STATIC_LIBRARY``: build mp++ as a static library, instead
  of a dynamic library (off by default),
* ``MPPP_MSVC_UNICODE``: enable Unicode solutions for MSVC (available only
  when using MSVC, off by default).

Note that the ``MPPP_WITH_QUADMATH`` option, at this time, is available only
using GCC (all the supported versions) and Clang
(since version 3.9). When using Clang, it is still necessary to link to the
quadmath library from GCC.

To build mp++, you can run the following CMake command from the
build directory:

.. code-block:: console

   $ cmake --build .

To install mp++, you can use the following CMake command:

.. code-block:: console

   $ cmake  --build . --target install

The installation command will copy the mp++ headers and library to the
``CMAKE_INSTALL_PREFIX`` directory.

If you enabled the ``MPPP_BUILD_TESTS`` option, you can run the test suite
with the following command:

.. code-block:: console

   $ cmake  --build . --target test

If you enabled the ``MPPP_BUILD_BENCHMARKS`` option, you can run the benchmark
suite with the following command:

.. code-block:: console

   $ cmake  --build . --target benchmark

.. note::

   On Windows, and if mp++ is built as a shared library (the default),
   in order to execute the test or the benchmark suite you have to ensure that the
   ``PATH`` variable includes the directory that contains the mp++
   DLL (otherwise the tests will fail to run).


Packages
--------

mp++ is also available from a variety of package managers on various platforms.

Conda
^^^^^

.. versionadded:: 0.2

mp++ is available in the `conda <https://conda.io/en/latest/>`__ package manager from the
`conda-forge <https://conda-forge.org/>`__ channel. Packages for Linux, Windows
and OSX are available. In order to install mp++ via conda, you just need
to add ``conda-forge`` to the channels:

.. code-block:: console

   $ conda config --add channels conda-forge
   $ conda install mppp

(note that the `conda package <https://anaconda.org/conda-forge/mppp>`__ for mp++ is named ``mppp`` rather than ``mp++``)

Please refer to the `conda documentation <https://conda.io/en/latest/>`__ for instructions on how to setup and manage
your conda installation.

FreeBSD
^^^^^^^

A FreeBSD port via `pkg
<https://www.freebsd.org/doc/handbook/pkgng-intro.html>`__ has been created for
mp++. In order to install mp++ using pkg, execute the following command:

.. code-block:: console

   $ pkg install mppp


Checking the installation
-------------------------

You can test the installation of mp++ with the following
simple ``main.cpp`` program:

.. code-block:: c++

   #include <iostream>
   #include <mp++/mp++.hpp>

   using int_t = mppp::integer<1>;

   int main()
   {
       int_t n{42};
       std::cout << n << '\n';
   }

If mp++ is installed in a standard prefix, on a typical GNU/Linux
system you can compile this example with the following command:

.. code-block:: console

   $ g++ -std=c++11 main.cpp -lmp++ -lgmp

.. note::

   The ``-std=c++11`` flag is not necessary if your GCC version is recent enough (i.e., for GCC 6 and later).

If you installed mp++ with optional features enabled, you will
need to link the required libraries as well. For instance,
if both MPFR and quadmath support are enabled, the compilation
command on a modern GNU/Linux system will be something like:

.. code-block:: console

   $ g++ -std=c++11 main.cpp -lmp++ -lquadmath -lmpfr -lgmp

.. note::

   Unless the definition ``NDEBUG`` is activated at compile time, mp++ runs extensive
   internal debug checks at runtime which carry a large performance penalty. Users are advised
   to always define ``NDEBUG`` when compiling code using mp++ in ``Release`` builds.

The full list of libraries that need to be linked when
using mp++ is the following:

* the GMP library (or the MPIR fork), always required
  (``-lgmp`` on most Unix-like systems);
* the MPFR library, required only if mp++ was configured with
  the ``MPPP_WITH_MPFR`` option (``-lmpfr`` on most Unix-like systems);
* the quadmath library, required only if mp++ was configured with the
  ``MPPP_WITH_QUADMATH`` option (``-lquadmath`` with GCC,
  with clang it might be necessary to provide the full path to the library).

If you are using CMake, it's highly recommended to make use of the config-file
package provided with mp++ rather
than locating and linking manually the required dependencies
(see the next section).

Including mp++ in your project via CMake
----------------------------------------

.. versionadded:: 0.2

As a part of the mp++ installation, a group of CMake files is installed into
``CMAKE_INSTALL_PREFIX/lib/cmake/mp++``.
This bundle, which is known in the CMake lingo as a `config-file package <https://cmake.org/cmake/help/v3.3/manual/cmake-packages.7.html>`__,
facilitates the detection and use of mp++ from other CMake-based projects.
mp++'s config-file package, once loaded, provides
an imported target called ``mp++::mp++`` which encapsulates all the information
necessary to use mp++. That is, linking to
``mp++::mp++`` ensures that mp++'s include directories are added to the include
path of the compiler, and that the libraries
on which mp++ depends (e.g., GMP) are brought into the link chain.

For instance, a ``CMakeLists.txt`` file for the simple ``main.cpp``
program presented earlier may look like this:

.. code-block:: cmake

   # mp++ needs at least CMake 3.3.
   cmake_minimum_required(VERSION 3.3.0)

   # The name of our project.
   project(sample_project)

   # Look for an installation of mp++ in the system.
   find_package(mp++ REQUIRED)

   # Create an executable, and link it to the mp++::mp++ imported target.
   # This ensures that, in the compilation of 'main', mp++'s include
   # dirs are added to the include path of the compiler and that mp++'s
   # dependencies (e.g., GMP) are transitively linked to 'main'.
   add_executable(main main.cpp)
   target_link_libraries(main mp++::mp++)

   # This line indicates to your compiler
   # that C++11 is needed for the compilation.
   # Not strictly necessary with a recent-enough compiler.
   set_property(TARGET main PROPERTY CXX_STANDARD 11)

Compiler and platform specific notes
------------------------------------

Visual Studio:

* The mp++ library is compiled with the ``NOMINMAX`` and
  ``WIN32_LEAN_AND_MEAN`` definitions, and,
  if supported, with the ``/permissive-`` compiler flag.
* If the ``MPPP_MSVC_UNICODE`` CMake option is enabled, the mp++ library
  is compiled with the ``UNICODE`` and ``_UNICODE`` definitions.
* When building mp++ as a static library, MSVC's static runtime will
  be used (instead of the dynamic runtime). One can force the use
  of the dynamic runtime when building mp++ as a static library by
  turning on the ``MPPP_BUILD_STATIC_LIBRARY_WITH_DYNAMIC_MSVC_RUNTIME``
  advanced CMake option.

MinGW:

* Due to a compiler bug in the implementation of ``thread_local``
  storage [#mingw_tls]_,
  certain multi-threading performance optimisations are disabled
  when compiling with MinGW.

OSX:

* When using older versions of Xcode, performance in multi-threading
  scenarios might be reduced due to lack of support for the C++11
  ``thread_local`` feature.

FreeBSD:

* The ``long double`` overloads of some mathematical functions
  (such as ``std::pow()``) may be implemented in
  ``double`` precision. Additionally, if the arguments to such mathematical
  functions are compile-time constants, the compiler
  *may* decide (depending on the optimisation level) to actually compute the
  result at compile time using full ``long double`` precision.
  This behaviour can lead to subtle inconsistencies, and it results in one
  test case from the mp++ test suite failing on FreeBSD [#freebsd_mppp_bug]_.

.. rubric:: Footnotes

.. [#mingw_tls] https://sourceforge.net/p/mingw-w64/bugs/445/
.. [#freebsd_mppp_bug] https://github.com/bluescarni/mppp/issues/132
