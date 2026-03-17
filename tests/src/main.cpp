#include <gtest/gtest.h>
#include "TestUtils.hpp"

int main(int argc, char** argv)
{
    ScopedStreamSilencer silence_out(std::cout);
    ScopedStreamSilencer silence_err(std::cerr);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}