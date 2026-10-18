// Tiny dependency-free SVG chart writer (line + scatter charts with axes).
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace swingtrack {

using Point = std::pair<double, double>;

class SvgChart {
 public:
  SvgChart(int width, int height, std::string title, std::string xLabel,
           std::string yLabel);

  void setXRange(double lo, double hi);
  void setYRange(double lo, double hi);

  void addLine(const std::vector<Point> &pts, const std::string &color,
               double width = 1.5);
  void addPoints(const std::vector<Point> &pts, const std::string &color,
                 double radius = 3.5);
  // Shaded vertical band from x0 to x1 (data units).
  void addBand(double x0, double x1, const std::string &color,
               double opacity = 0.18);
  void addHLine(double y, const std::string &color, const std::string &label,
                bool dashed = true);
  void addVLine(double x, const std::string &color, const std::string &label);
  void addLegend(const std::string &label, const std::string &color);

  std::string render() const;
  void save(const std::string &path) const;

 private:
  double sx(double x) const;
  double sy(double y) const;

  int w_, h_;
  std::string title_, xLabel_, yLabel_;
  double x0_ = 0, x1_ = 1, y0_ = 0, y1_ = 1;
  std::vector<std::string> body_;
  std::vector<std::pair<std::string, std::string>> legend_;
};

// "Nice" tick positions covering [lo, hi], roughly `count` of them.
std::vector<double> niceTicks(double lo, double hi, int count = 6);

}  // namespace swingtrack
