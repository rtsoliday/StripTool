#include "core/model.h"

namespace striptool {

bool Rgba16::operator==(const Rgba16& other) const {
  return red == other.red && green == other.green && blue == other.blue &&
         alpha == other.alpha;
}

StripToolModel makeDefaultModel() {
  StripToolModel model;
  model.colors.curves = {{{0, 0, 65535, 65535},
                          {32896, 32896, 0, 65535},
                          {42405, 10794, 10794, 65535},
                          {24415, 40606, 41120, 65535},
                          {65535, 42405, 0, 65535},
                          {41120, 8224, 41120, 65535},
                          {65535, 0, 0, 65535},
                          {65535, 55255, 0, 65535},
                          {48316, 36751, 36751, 65535},
                          {39578, 52685, 12850, 65535}}};
  for (std::size_t i = 0; i < model.curves.size(); ++i) {
    model.curves[i].name = "Curve" + std::to_string(i);
  }
  return model;
}

}  // namespace striptool
