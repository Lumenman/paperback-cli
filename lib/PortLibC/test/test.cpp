/*
 * =====================================================================================
 *
 *       Filename:  test.cpp
 *
 *    Description:  Unit Tests using Boost.Test
 *
 *        Version:  1.0 
 *        Created:  08/01/2017 13:49:04 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  suhrke@teknik.io
 *
 * =====================================================================================
 */

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE PortLibCpp
#include <boost/test/unit_test.hpp>
#include "test-fileattributes.cpp"
#include "test-bitmap.cpp"
#include "test-borland.cpp"

