// Copyright (c) 2017, Oracle and/or its affiliates. All rights reserved.
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software
// Foundation; version 2 of the License.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, 51 Franklin
// Street, Suite 500, Boston, MA 02110-1335 USA.

#include "wkb_parser.h"

#include <cmath>  // M_PI, M_PI_2
#include <exception>

#include "coordinate_range_visitor.h"
#include "geometries.h"
#include "geometries_cs.h"
#include "my_byteorder.h"
#include "my_sys.h"  // my_error()
#include "mysqld_error.h"
#include "ring_flip_visitor.h"
#include "sql_error.h"
#include "sql_string.h"
#include "srs_fetcher.h"
#include "template_utils.h"  // pointer_cast

namespace gis {

/// WKB endianness.
enum class Byte_order : std::uint8_t {
  /// Big endian
  XDR = 0,
  /// Little endian
  NDR = 1
};

/// Checks if a given type is a valid (and supported) WKB type.
///
/// @param type The type to check
///
/// @retval true The type is valid.
/// @retval false The type is invalid.
static bool is_valid_type(Geometry_type type) {
  switch (type) {
    case Geometry_type::kPoint:
    case Geometry_type::kLinestring:
    case Geometry_type::kPolygon:
    case Geometry_type::kMultipoint:
    case Geometry_type::kMultilinestring:
    case Geometry_type::kMultipolygon:
    case Geometry_type::kGeometrycollection:
      return true;
    default:
      return false; /* purecov: inspected */
  }
}

/// Checks if a given type is a subtype of a given supertype.
///
/// @param sub The type to check.
/// @param super The supertype.
///
/// @retval true The type is the supertype or a subtype of it.
/// @retval false The type is neither the supertype nor a subtype of it.
static bool is_subtype_of(Geometry_type sub, Geometry_type super) {
  return (super == Geometry_type::kGeometry || sub == super ||
          (super == Geometry_type::kGeometrycollection &&
           (sub == Geometry_type::kMultipoint ||
            sub == Geometry_type::kMultilinestring ||
            sub == Geometry_type::kMultipolygon)));
}

/// Checks if a given type is a valid type and that it is a subtype of a given
/// supertype.
///
/// @param sub The type to check.
/// @param super The supertype.
///
/// @retval true The type is a valid subtype of the supertype.
/// @retval false The type is invalid or not a subtype of the supertype.
static bool is_valid_type_or_subtype(Geometry_type sub, Geometry_type super) {
  return is_valid_type(sub) && is_subtype_of(sub, super);
}

template <typename Point_t, typename Linestring_t, typename Linearring_t,
          typename Polygon_t, typename Geometrycollection_t,
          typename Multipoint_t, typename Multilinestring_t,
          typename Multipolygon_t>
class Wkb_parser {
 private:
  uchar *m_begin;
  uchar *m_end;
  Coordinate_system m_coordinate_system;
  double m_angular_unit;
  double m_prime_meridian;
  bool m_positive_north;
  bool m_positive_east;
  bool m_swap_axes;

  double transform_x(double x) {
    DBUG_ASSERT(!std::isnan(x));
    switch (m_coordinate_system) {
      case Coordinate_system::kCartesian:
        // The on-disk and in-memory format is x in SRS direction and unit.
        break;
      case Coordinate_system::kGeographic:
        // The on-disk format is x = longitude, in the SRS direction and unit,
        // and with the SRS meridian.
        // The in-memory format is x = longitude (Easting) in radians with the
        // meridian at Greenwich.
        if (!m_positive_east) x *= -1.0;
        x += m_prime_meridian;  // Both are in the SRS angular unit
        x *= m_angular_unit;    // Convert to radians
        break;
      default:
        DBUG_ASSERT(false); /* purecov: inspected */
        break;
    }

    DBUG_ASSERT(!std::isnan(x));
    return x;
  }

  double transform_y(double y) {
    DBUG_ASSERT(!std::isnan(y));
    switch (m_coordinate_system) {
      case Coordinate_system::kCartesian:
        // The on-disk and in-memory format is y in SRS direction and unit.
        break;
      case Coordinate_system::kGeographic:
        // The on-disk format is y = latitude, in the SRS direction and unit.
        // The in-memory format is y = latitude (Northing) in radians.
        if (!m_positive_north) y *= -1.0;
        y *= m_angular_unit;  // Convert to radians
        break;
      default:
        DBUG_ASSERT(false); /* purecov: inspected */
        break;
    }

    DBUG_ASSERT(!std::isnan(y));
    return y;
  }

 public:
  Wkb_parser(const dd::Spatial_reference_system *srs, bool ignore_axis_order,
             uchar *begin, uchar *end)
      : m_begin(begin),
        m_end(end),
        m_coordinate_system(Coordinate_system::kCartesian),
        m_angular_unit(1.0),
        m_prime_meridian(0.0),
        m_positive_north(true),
        m_positive_east(true),
        m_swap_axes(false) {
    if (srs == nullptr || srs->is_cartesian()) {
      m_coordinate_system = Coordinate_system::kCartesian;
    } else if (srs->is_geographic()) {
      m_coordinate_system = Coordinate_system::kGeographic;
      m_angular_unit = srs->angular_unit();
      m_prime_meridian = srs->prime_meridian();
      m_positive_north = srs->positive_north();
      m_positive_east = srs->positive_east();
      if (!ignore_axis_order && srs->is_lat_long()) m_swap_axes = true;
    }
  }
  Byte_order parse_byte_order() {
    if (m_begin + 1 > m_end) throw std::exception();

    switch (*(m_begin++)) {
      case 0:
        return Byte_order::XDR;
      case 1:
        return Byte_order::NDR;
    }

    throw std::exception(); /* purecov: inspected */
  }

  bool reached_end() const { return m_begin == m_end; }

  std::uint32_t parse_uint32(Byte_order bo) {
    if (m_begin + sizeof(std::uint32_t) > m_end) throw std::exception();

    std::uint32_t i;
    if (bo == Byte_order::NDR) {
      i = uint4korr(m_begin);
    } else {
      i = load32be(m_begin);
    }

    m_begin += 4;
    return i;
  }

  double parse_double(Byte_order bo) {
    if (m_begin + sizeof(double) > m_end) throw std::exception();

    double d;
    if (bo == Byte_order::NDR) {
      // Little endian data. Use conversion functions to native endianness.
      float8get(&d, m_begin);
    } else {
#ifdef WORDS_BIGENDIAN
      // Both data and native endianness is big endian. No need to convert.
      memcpy(&d, m_begin, sizeof(double));
#else
      // Big endian data on little endian CPU. Convert to little endian.
      uchar inv_array[8];
      inv_array[0] = m_begin[7];
      inv_array[1] = m_begin[6];
      inv_array[2] = m_begin[5];
      inv_array[3] = m_begin[4];
      inv_array[4] = m_begin[3];
      inv_array[5] = m_begin[2];
      inv_array[6] = m_begin[1];
      inv_array[7] = m_begin[0];
      float8get(&d, inv_array);
#endif
    }

    m_begin += sizeof(double);
    return d;
  }

  Geometry_type parse_geometry_type(Byte_order bo) {
    if (m_begin + sizeof(std::uint32_t) > m_end) {
      throw std::exception();
    }

    std::uint32_t wkb_type = parse_uint32(bo);
    Geometry_type type = static_cast<Geometry_type>(wkb_type);

    if (!is_valid_type_or_subtype(type, Geometry_type::kGeometry))
      throw std::exception();
    return type;
  }

  Point_t parse_point(Byte_order bo) {
    double x = parse_double(bo);
    double y = parse_double(bo);
    if (!std::isfinite(x) || !std::isfinite(y)) throw std::exception();
    if (m_swap_axes)
      return Point_t(transform_x(y), transform_y(x));
    else
      return Point_t(transform_x(x), transform_y(y));
  }

  Point_t parse_wkb_point() {
    Byte_order bo = parse_byte_order();
    Geometry_type type = parse_geometry_type(bo);
    if (type != Geometry_type::kPoint) throw std::exception();
    return parse_point(bo);
  }

  Linestring_t parse_linestring(Byte_order bo) {
    Linestring_t ls;
    std::uint32_t num_points = parse_uint32(bo);
    if (num_points < 2) throw std::exception();
    for (std::uint32_t i = 0; i < num_points; i++) {
      ls.push_back(parse_point(bo));
    }
    return std::move(ls);
  }

  Linestring_t parse_wkb_linestring() {
    Byte_order bo = parse_byte_order();
    Geometry_type type = parse_geometry_type(bo);
    if (type != Geometry_type::kLinestring) throw std::exception();
    return parse_linestring(bo);
  }

  Polygon_t parse_polygon(Byte_order bo) {
    Polygon_t py;
    std::uint32_t num_rings = parse_uint32(bo);
    if (num_rings == 0) throw std::exception();
    for (std::uint32_t i = 0; i < num_rings; i++) {
      Linearring_t lr;
      std::uint32_t num_points = parse_uint32(bo);
      if (num_points < 4) throw std::exception();
      for (std::uint32_t j = 0; j < num_points; j++) {
        lr.push_back(parse_point(bo));
      }
      py.push_back(std::forward<gis::Linearring>(lr));
    }
    return std::move(py);
  }

  Polygon_t parse_wkb_polygon() {
    Byte_order bo = parse_byte_order();
    Geometry_type type = parse_geometry_type(bo);

    if (type != Geometry_type::kPolygon) throw std::exception();
    return parse_polygon(bo);
  }

  Multipoint_t parse_multipoint(Byte_order bo) {
    Multipoint_t mpt;
    std::uint32_t num_points = parse_uint32(bo);
    if (num_points == 0) throw std::exception();
    for (std::uint32_t i = 0; i < num_points; i++) {
      mpt.push_back(parse_wkb_point());
    }
    return std::move(mpt);
  }

  Multilinestring_t parse_multilinestring(Byte_order bo) {
    Multilinestring_t mls;
    std::uint32_t num_linestrings = parse_uint32(bo);
    if (num_linestrings == 0) throw std::exception();
    for (std::uint32_t i = 0; i < num_linestrings; i++) {
      Linestring_t ls;
      mls.push_back(parse_wkb_linestring());
    }
    return std::move(mls);
  }

  Multipolygon_t parse_multipolygon(Byte_order bo) {
    Multipolygon_t mpy;
    std::uint32_t num_polygons = parse_uint32(bo);
    if (num_polygons == 0) throw std::exception();
    for (std::uint32_t i = 0; i < num_polygons; i++) {
      mpy.push_back(parse_wkb_polygon());
    }
    return std::move(mpy);
  }

  Geometrycollection_t parse_geometrycollection(Byte_order bo) {
    Geometrycollection_t gc;
    std::uint32_t num_geometries = parse_uint32(bo);
    for (std::uint32_t i = 0; i < num_geometries; i++) {
      Geometry *g = parse_wkb();
      gc.push_back(std::move(*g));
      delete g;
    }
    return std::move(gc);
  }

  Geometry *parse_wkb() {
    Byte_order bo = parse_byte_order();
    Geometry_type type = parse_geometry_type(bo);

    switch (type) {
      case Geometry_type::kPoint:
        return new Point_t(parse_point(bo));
      case Geometry_type::kLinestring:
        return new Linestring_t(parse_linestring(bo));
      case Geometry_type::kPolygon:
        return new Polygon_t(parse_polygon(bo));
      case Geometry_type::kMultipoint:
        return new Multipoint_t(parse_multipoint(bo));
      case Geometry_type::kMultilinestring:
        return new Multilinestring_t(parse_multilinestring(bo));
      case Geometry_type::kMultipolygon:
        return new Multipolygon_t(parse_multipolygon(bo));
      case Geometry_type::kGeometrycollection:
        return new Geometrycollection_t(parse_geometrycollection(bo));
      default:
        throw std::exception(); /* purecov: inspected */
    }
  }
};

std::unique_ptr<Geometry> parse_wkb(const dd::Spatial_reference_system *srs,
                                    const char *wkb, std::size_t length,
                                    bool ignore_axis_order) {
  unsigned char *begin = pointer_cast<unsigned char *>(const_cast<char *>(wkb));
  unsigned char *end = begin + length;
  std::unique_ptr<Geometry> g = nullptr;
  bool res;

  if (srs == nullptr || srs->is_cartesian()) {
    try {
      Wkb_parser<Cartesian_point, Cartesian_linestring, Cartesian_linearring,
                 Cartesian_polygon, Cartesian_geometrycollection,
                 Cartesian_multipoint, Cartesian_multilinestring,
                 Cartesian_multipolygon>
          parser(srs, ignore_axis_order, begin, end);
      g.reset(parser.parse_wkb());
      res = !g || !parser.reached_end();
    } catch (...) {
      res = true;
    }
  } else if (srs->is_geographic()) {
    try {
      Wkb_parser<Geographic_point, Geographic_linestring, Geographic_linearring,
                 Geographic_polygon, Geographic_geometrycollection,
                 Geographic_multipoint, Geographic_multilinestring,
                 Geographic_multipolygon>
          parser(srs, ignore_axis_order, begin, end);
      g.reset(parser.parse_wkb());
      res = !g || !parser.reached_end();
    } catch (...) {
      res = true;
    }
  } else {
    DBUG_ASSERT(false); /* purecov: inspected */
    return std::unique_ptr<Geometry>();
  }

  if (res) {
    return std::unique_ptr<Geometry>();
  }

  return g;
}

bool parse_srid(const char *str, std::size_t length, srid_t *srid) {
  unsigned char *begin = pointer_cast<unsigned char *>(const_cast<char *>(str));

  if (length < sizeof(srid_t)) return true;
  *srid = uint4korr(begin);  // Always little-endian.
  return false;
}

bool parse_geometry(THD *thd, const char *func_name, const String *str,
                    const dd::Spatial_reference_system **srs,
                    std::unique_ptr<Geometry> *geometry) {
  srid_t srid;
  if (parse_srid(str->ptr(), str->length(), &srid)) {
    my_error(ER_GIS_INVALID_DATA, MYF(0), func_name);
    return true;
  }

  Srs_fetcher fetcher(thd);
  *srs = nullptr;
  if (srid != 0 && fetcher.acquire(srid, srs)) return true;

  if (srid != 0 && *srs == nullptr) {
    my_error(ER_SRS_NOT_FOUND, MYF(0), srid);
    return true;
  }

  *geometry = gis::parse_wkb(*srs, str->ptr() + sizeof(srid_t),
                             str->length() - sizeof(srid_t), true);
  if (!(*geometry)) {
    // Parsing failed, assume invalid input data.
    my_error(ER_GIS_INVALID_DATA, MYF(0), func_name);
    return true;
  }

  // Flip polygon rings so that the exterior ring is counterclockwise and
  // interior rings are clockwise.
  gis::Ring_flip_visitor rfv(*srs, gis::Ring_direction::kCCW);
  (*geometry)->accept(&rfv);
  if ((*srs == nullptr || (*srs)->is_cartesian()) && rfv.invalid()) {
    // There's something wrong with a polygon in the geometry.
    my_error(ER_GIS_INVALID_DATA, MYF(0), func_name);
    return true;
  }

  gis::Coordinate_range_visitor crv(*srs);
  if ((*geometry)->accept(&crv)) {
    if (crv.longitude_out_of_range()) {
      my_error(ER_LONGITUDE_OUT_OF_RANGE, MYF(0), crv.coordinate_value(),
               func_name, (*srs)->from_radians(-M_PI),
               (*srs)->from_radians(M_PI));
      return true;
    }
    if (crv.latitude_out_of_range()) {
      my_error(ER_LATITUDE_OUT_OF_RANGE, MYF(0), crv.coordinate_value(),
               func_name, (*srs)->from_radians(-M_PI_2),
               (*srs)->from_radians(M_PI_2));
      return true;
    }
  }

  return false;
}

}  // gis
