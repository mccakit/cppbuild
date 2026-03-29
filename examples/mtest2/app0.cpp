#include <iostream>
extern "C" {
#include "myc.h"
}
#include "gen1.hpp"
#include "tgt1.hpp"
import a;
import h1;
import gen0;
import tgt1_mod;

int main() {
  std::cout << run() << std::endl;
  std::cout << h1() << std::endl;
  std::cout << add_c(1, 2) << std::endl;
  print_module_msg();
  gen1::print_pair_msg();
  std::cout << tgt1_header_func() << std::endl;
  std::cout << tgt1_module_func() << std::endl;
  std::cout << tgt1_hu_func() << std::endl;
}
