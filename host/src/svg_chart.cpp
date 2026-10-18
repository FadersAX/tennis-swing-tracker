#include "swingtrack/svg_chart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace swingtrack {
namespace {

const int kLeft = 70, kRight = 24, kTop = 44, kBottom = 52;

std::string num(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.1f", v);
  return buf;
}

std::string tickLabel(double v) {
  char buf[32];
  if (std::fabs(v) >= 100 || v == std::floor(v)) {
    std::snprintf(buf, sizeof(buf), "%.0f", v);
  } else {
    std::snprintf(buf, sizeof(buf), "%.2g", v);
  }
  return buf;
}

std::string escape(const std::string &s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '&': out += "&amp;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

}  // namespace

std::vector<double> niceTicks(double lo, double hi, int count) {
  std::vector<double> ticks;
  if (!(hi > lo) || count < 2) return ticks;
  const double rough = (hi - lo) / (count - 1);
  const double mag = std::pow(10.0, std::floor(std::log10(rough)));
  const double norm = rough / mag;
  const double step = (norm < 1.5 ? 1 : norm < 3 ? 2 : norm < 7 ? 5 : 10) * mag;
  for (double t = std::ceil(lo / step) * step; t <= hi + step * 1e-9; t += step) {
    ticks.push_back(std::fabs(t) < step * 1e-9 ? 0.0 : t);
  }
  return ticks;
}

SvgChart::SvgChart(int width, int height, std::string title,
                   std::string xLabel, std::string yLabel)
    : w_(width), h_(height), title_(std::move(title)),
      xLabel_(std::move(xLabel)), yLabel_(std::move(yLabel)) {}

void SvgChart::setXRange(double lo, double hi) {
  x0_ = lo;
  x1_ = hi > lo ? hi : lo + 1;
}

void SvgChart::setYRange(double lo, double hi) {
  y0_ = lo;
  y1_ = hi > lo ? hi : lo + 1;
}

double SvgChart::sx(double x) const {
  return kLeft + (x - x0_) / (x1_ - x0_) * (w_ - kLeft - kRight);
}

double SvgChart::sy(double y) const {
  return h_ - kBottom - (y - y0_) / (y1_ - y0_) * (h_ - kTop - kBottom);
}

void SvgChart::addLine(const std::vector<Point> &pts, const std::string &color,
                       double width) {
  if (pts.empty()) return;
  std::ostringstream o;
  o << "<polyline fill=\"none\" stroke=\"" << color << "\" stroke-width=\""
    << width << "\" stroke-linejoin=\"round\" points=\"";
  for (const auto &[x, y] : pts) o << num(sx(x)) << ',' << num(sy(y)) << ' ';
  o << "\"/>";
  body_.push_back(o.str());
}

void SvgChart::addPoints(const std::vector<Point> &pts,
                         const std::string &color, double radius) {
  std::ostringstream o;
  o << "<g fill=\"" << color << "\">";
  for (const auto &[x, y] : pts) {
    o << "<circle cx=\"" << num(sx(x)) << "\" cy=\"" << num(sy(y))
      << "\" r=\"" << radius << "\"/>";
  }
  o << "</g>";
  body_.push_back(o.str());
}

void SvgChart::addBand(double x0, double x1, const std::string &color,
                       double opacity) {
  std::ostringstream o;
  o << "<rect x=\"" << num(sx(x0)) << "\" y=\"" << kTop << "\" width=\""
    << num(std::max(1.0, sx(x1) - sx(x0))) << "\" height=\""
    << (h_ - kTop - kBottom) << "\" fill=\"" << color << "\" opacity=\""
    << opacity << "\"/>";
  body_.push_back(o.str());
}

void SvgChart::addHLine(double y, const std::string &color,
                        const std::string &label, bool dashed) {
  std::ostringstream o;
  o << "<line x1=\"" << kLeft << "\" x2=\"" << (w_ - kRight) << "\" y1=\""
    << num(sy(y)) << "\" y2=\"" << num(sy(y)) << "\" stroke=\"" << color
    << "\" stroke-width=\"1.2\"" << (dashed ? " stroke-dasharray=\"5 4\"" : "")
    << "/>";
  if (!label.empty()) {
    o << "<text x=\"" << (w_ - kRight - 4) << "\" y=\"" << num(sy(y) - 4)
      << "\" text-anchor=\"end\" fill=\"" << color << "\" font-size=\"11\">"
      << escape(label) << "</text>";
  }
  body_.push_back(o.str());
}

void SvgChart::addVLine(double x, const std::string &color,
                        const std::string &label) {
  std::ostringstream o;
  o << "<line x1=\"" << num(sx(x)) << "\" x2=\"" << num(sx(x)) << "\" y1=\""
    << kTop << "\" y2=\"" << (h_ - kBottom) << "\" stroke=\"" << color
    << "\" stroke-width=\"1.2\" stroke-dasharray=\"3 3\"/>";
  if (!label.empty()) {
    o << "<text x=\"" << num(sx(x) + 4) << "\" y=\"" << (kTop + 12)
      << "\" fill=\"" << color << "\" font-size=\"11\">" << escape(label)
      << "</text>";
  }
  body_.push_back(o.str());
}

void SvgChart::addLegend(const std::string &label, const std::string &color) {
  legend_.emplace_back(label, color);
}

std::string SvgChart::render() const {
  std::ostringstream o;
  o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << w_
    << "\" height=\"" << h_ << "\" viewBox=\"0 0 " << w_ << ' ' << h_
    << "\" font-family=\"Segoe UI, Helvetica, Arial, sans-serif\">\n"
    << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n"
    << "<text x=\"" << kLeft << "\" y=\"26\" font-size=\"16\" "
    << "font-weight=\"600\" fill=\"#1f2328\">" << escape(title_) << "</text>\n";

  // Grid and tick labels
  for (double t : niceTicks(y0_, y1_)) {
    o << "<line x1=\"" << kLeft << "\" x2=\"" << (w_ - kRight) << "\" y1=\""
      << num(sy(t)) << "\" y2=\"" << num(sy(t))
      << "\" stroke=\"#e6e8eb\"/><text x=\"" << (kLeft - 8) << "\" y=\""
      << num(sy(t) + 4) << "\" text-anchor=\"end\" font-size=\"11\" "
      << "fill=\"#57606a\">" << tickLabel(t) << "</text>\n";
  }
  for (double t : niceTicks(x0_, x1_)) {
    o << "<line x1=\"" << num(sx(t)) << "\" x2=\"" << num(sx(t)) << "\" y1=\""
      << (h_ - kBottom) << "\" y2=\"" << (h_ - kBottom + 5)
      << "\" stroke=\"#8c959f\"/><text x=\"" << num(sx(t)) << "\" y=\""
      << (h_ - kBottom + 18) << "\" text-anchor=\"middle\" font-size=\"11\" "
      << "fill=\"#57606a\">" << tickLabel(t) << "</text>\n";
  }

  // Plot area clip so lines never spill over the axes.
  o << "<clipPath id=\"plot\"><rect x=\"" << kLeft << "\" y=\"" << kTop
    << "\" width=\"" << (w_ - kLeft - kRight) << "\" height=\""
    << (h_ - kTop - kBottom) << "\"/></clipPath>\n<g clip-path=\"url(#plot)\">\n";
  for (const auto &b : body_) o << b << '\n';
  o << "</g>\n";

  // Axes
  o << "<line x1=\"" << kLeft << "\" x2=\"" << kLeft << "\" y1=\"" << kTop
    << "\" y2=\"" << (h_ - kBottom) << "\" stroke=\"#8c959f\"/>\n"
    << "<line x1=\"" << kLeft << "\" x2=\"" << (w_ - kRight) << "\" y1=\""
    << (h_ - kBottom) << "\" y2=\"" << (h_ - kBottom)
    << "\" stroke=\"#8c959f\"/>\n"
    << "<text x=\"" << (kLeft + (w_ - kLeft - kRight) / 2) << "\" y=\""
    << (h_ - 12) << "\" text-anchor=\"middle\" font-size=\"12\" "
    << "fill=\"#1f2328\">" << escape(xLabel_) << "</text>\n"
    << "<text transform=\"translate(18 " << (kTop + (h_ - kTop - kBottom) / 2)
    << ") rotate(-90)\" text-anchor=\"middle\" font-size=\"12\" "
    << "fill=\"#1f2328\">" << escape(yLabel_) << "</text>\n";

  // Legend, top right
  double lx = w_ - kRight;
  for (auto it = legend_.rbegin(); it != legend_.rend(); ++it) {
    lx -= 22 + 7.0 * it->first.size();
    o << "<circle cx=\"" << num(lx) << "\" cy=\"22\" r=\"5\" fill=\""
      << it->second << "\"/><text x=\"" << num(lx + 9)
      << "\" y=\"26\" font-size=\"12\" fill=\"#1f2328\">" << escape(it->first)
      << "</text>\n";
  }
  o << "</svg>\n";
  return o.str();
}

void SvgChart::save(const std::string &path) const {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write " + path);
  out << render();
}

}  // namespace swingtrack
