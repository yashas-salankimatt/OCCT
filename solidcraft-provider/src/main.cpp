// solidcraft-occt-provider: a geometry provider for SolidCraft, backed by Open CASCADE
// Technology. SolidCraft runs it as a separate program when its own kernel cannot do a Boolean
// (SolidCraft's docs/geometry-providers.md). It speaks solidcraft-geometry-provider/1: it reads
// one JSON request line from standard input, writes one JSON reply line to standard output and
// exits.
//
//   {"protocol", "op": "hello"}
//     -> {"ok": true, "provider": "occt", "version": "8.0.1", "operations": ["boolean"]}
//   {"protocol", "op": "boolean", "kind": "union" | "cut" | "intersect",
//    "operands": [STEP, STEP], "tolerance_mm": t}
//     -> {"ok": true, "step": STEP}  or  {"ok": false, "error": "..."}
//
// Operands and results are STEP text in millimetres. Anything the libraries print goes to
// standard error: standard output carries only the reply.
//
// Part of solidcraft-occt-provider. Copyright the contributors. Distributed under the GNU Lesser
// General Public License version 2.1 with the Open CASCADE exception, as Open CASCADE Technology.

#include "json.h"

#include <BRepAlgoAPI_BooleanOperation.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <ShapeFix_Shape.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TopExp_Explorer.hxx>
#include <NCollection_List.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Pnt.hxx>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
  #include <io.h>
  #define dup _dup
  #define dup2 _dup2
  #define fileno _fileno
#else
  #include <unistd.h>
#endif

namespace
{

const char* const Protocol = "solidcraft-geometry-provider/1";

//! Largest request read (two STEP operands).
constexpr size_t MaxRequestBytes = size_t(512) << 20;

struct Failure : std::runtime_error
{
  using std::runtime_error::runtime_error;
};

//! The solids of `shape`, as one compound (or the solid itself when there is one).
TopoDS_Shape solids_of(const TopoDS_Shape& shape, int* count)
{
  BRep_Builder    builder;
  TopoDS_Compound compound;
  builder.MakeCompound(compound);
  int          n = 0;
  TopoDS_Shape last;
  for (TopExp_Explorer ex(shape, TopAbs_SOLID); ex.More(); ex.Next())
  {
    builder.Add(compound, ex.Current());
    last = ex.Current();
    ++n;
  }
  *count = n;
  return n == 1 ? last : TopoDS_Shape(compound);
}

TopoDS_Shape read_step(const std::string& text, const char* which)
{
  STEPControl_Reader reader;
  std::istringstream in(text);
  if (reader.ReadStream(which, in) != IFSelect_RetDone)
    throw Failure(std::string("the ") + which + "'s STEP could not be read");
  if (reader.TransferRoots() == 0)
    throw Failure(std::string("the ") + which + " has nothing to transfer");
  int                n      = 0;
  const TopoDS_Shape solids = solids_of(reader.OneShape(), &n);
  if (n == 0)
    throw Failure(std::string("the ") + which + " has no solid");
  return solids;
}

std::string write_step(const TopoDS_Shape& shape)
{
  STEPControl_Writer writer;
  Interface_Static::SetCVal("write.step.unit", "MM");
  if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone)
    throw Failure("the result could not be written as STEP");
  std::ostringstream out;
  if (writer.WriteStream(out) != IFSelect_RetDone)
    throw Failure("the result could not be written as STEP");
  return out.str();
}

double size_of(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
  Bnd_Box box;
  BRepBndLib::Add(a, box);
  BRepBndLib::Add(b, box);
  if (box.IsVoid())
    return 1.0;
  return std::sqrt(box.SquareExtent());
}

double volume_of(const TopoDS_Shape& shape)
{
  GProp_GProps props;
  BRepGProp::VolumeProperties(shape, props);
  return props.Mass();
}

std::unique_ptr<BRepAlgoAPI_BooleanOperation> operation(const std::string& kind)
{
  if (kind == "union")
    return std::make_unique<BRepAlgoAPI_Fuse>();
  if (kind == "cut")
    return std::make_unique<BRepAlgoAPI_Cut>();
  if (kind == "intersect")
    return std::make_unique<BRepAlgoAPI_Common>();
  throw Failure("unknown Boolean `" + kind + "` (union, cut or intersect)");
}

//! One attempt at the Boolean with `fuzzy` as the fuzzy tolerance (0: none). The result's
//! solids, valid (fixed when they are not), or a reason in `why`.
bool attempt(const std::string&  kind,
             const TopoDS_Shape& a,
             const TopoDS_Shape& b,
             double              fuzzy,
             TopoDS_Shape*       result,
             std::string*        why)
{
  std::unique_ptr<BRepAlgoAPI_BooleanOperation> op = operation(kind);
  NCollection_List<TopoDS_Shape>                args, tools;
  args.Append(a);
  tools.Append(b);
  op->SetArguments(args);
  op->SetTools(tools);
  op->SetRunParallel(true);
  op->SetNonDestructive(true);
  if (fuzzy > 0.0)
    op->SetFuzzyValue(fuzzy);
  op->Build();
  if (!op->IsDone() || op->HasErrors())
  {
    std::ostringstream errors;
    op->DumpErrors(errors);
    std::string e = errors.str();
    while (!e.empty() && (e.back() == '\n' || e.back() == ' '))
      e.pop_back();
    *why = e.empty() ? "the Boolean failed" : "the Boolean failed: " + e;
    return false;
  }
  int          n      = 0;
  TopoDS_Shape solids = solids_of(op->Shape(), &n);
  if (n == 0)
  {
    *why = "the Boolean left no solid";
    return false;
  }
  if (!BRepCheck_Analyzer(solids).IsValid())
  {
    Handle(ShapeFix_Shape) fix = new ShapeFix_Shape(solids);
    fix->Perform();
    solids = solids_of(fix->Shape(), &n);
    if (n == 0 || !BRepCheck_Analyzer(solids).IsValid())
    {
      *why = "the Boolean's result is not a valid solid, even after fixing";
      return false;
    }
  }
  if (!(volume_of(solids) > 0.0))
  {
    *why = "the Boolean's result has no volume";
    return false;
  }
  *result = solids;
  return true;
}

std::string boolean(const json::Value& request)
{
  const std::string kind = request["kind"].str();
  const json::Value& operands = request["operands"];
  if (operands.kind != json::Value::Kind::Array || operands.items.size() != 2)
    throw Failure("a Boolean takes two operands");
  const TopoDS_Shape a = read_step(operands.items[0].str(), "target");
  const TopoDS_Shape b = read_step(operands.items[1].str(), "tool");
  const double       size = size_of(a, b);
  double             tol  = request["tolerance_mm"].num(0.0);
  if (!(tol > 0.0) || !std::isfinite(tol))
    tol = 1e-7;
  // Exact first; then fuzzy tolerances for faces that only nearly touch, up to 1e-5 of the size.
  const double fuzzies[] = {0.0, tol, std::max(tol, size * 1e-6), std::max(tol, size * 1e-5)};
  std::string  why;
  for (const double fuzzy : fuzzies)
  {
    TopoDS_Shape result;
    if (attempt(kind, a, b, fuzzy, &result, &why))
      return "{\"ok\":true,\"step\":" + json::quote(write_step(result)) + "}";
  }
  throw Failure(why);
}

std::string answer(const std::string& line)
{
  const json::Value request = json::parse(line);
  if (request["protocol"].str() != Protocol)
    throw Failure(std::string("this program speaks ") + Protocol);
  const std::string op = request["op"].str();
  if (op == "hello")
    return std::string("{\"ok\":true,\"provider\":\"occt\",\"version\":") + json::quote(OCC_VERSION_COMPLETE)
           + ",\"operations\":[\"boolean\"]}";
  if (op == "boolean")
    return boolean(request);
  throw Failure("unknown request `" + op + "`");
}

//! `--self-test`: two boxes through the same STEP and JSON path as a request, each Boolean's
//! volume against its value worked out by hand. Prints what it checked; exit status 1 on failure.
int self_test()
{
  // A 20 x 10 x 10 block, and a 15 x 20 x 20 tool through its end: they share 5 x 10 x 10.
  const TopoDS_Shape a = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), gp_Pnt(20, 10, 10)).Shape();
  const TopoDS_Shape b = BRepPrimAPI_MakeBox(gp_Pnt(15, -5, -5), gp_Pnt(30, 15, 15)).Shape();
  const std::string  operands = "[" + json::quote(write_step(a)) + "," + json::quote(write_step(b)) + "]";
  const struct
  {
    const char* kind;
    double      volume;
  } cases[] = {{"union", 7500.0}, {"cut", 1500.0}, {"intersect", 500.0}};
  int failed = 0;
  for (const auto& c : cases)
  {
    const std::string request = std::string("{\"protocol\":\"") + Protocol + "\",\"op\":\"boolean\",\"kind\":\"" + c.kind
                                + "\",\"operands\":" + operands + ",\"tolerance_mm\":1e-6}";
    double got = -1.0;
    try
    {
      const json::Value reply = json::parse(answer(request));
      got                     = volume_of(read_step(reply["step"].str(), "result"));
    }
    catch (const std::exception& e)
    {
      std::cerr << c.kind << ": " << e.what() << "\n";
    }
    const bool ok = std::fabs(got - c.volume) <= 1e-6 * c.volume;
    std::cerr << (ok ? "ok   " : "FAIL ") << c.kind << ": volume " << got << " (expected " << c.volume << ")\n";
    failed += ok ? 0 : 1;
  }
  return failed == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv)
{
  if (argc > 1 && std::string(argv[1]) == "--self-test")
    return self_test();

  // Standard output carries only the reply: whatever else writes to it goes to standard error.
  std::fflush(stdout);
  const int reply_fd = dup(fileno(stdout));
  dup2(fileno(stderr), fileno(stdout));
  FILE* reply_out = reply_fd >= 0 ? fdopen(reply_fd, "w") : nullptr;
  if (reply_out == nullptr)
    return 2;

  std::string reply;
  try
  {
    std::string line;
    std::getline(std::cin, line);
    if (line.size() > MaxRequestBytes)
      throw Failure("the request is too large");
    reply = answer(line);
  }
  catch (const Standard_Failure& e)
  {
    reply = "{\"ok\":false,\"error\":" + json::quote(std::string("Open CASCADE: ") + e.what()) + "}";
  }
  catch (const std::exception& e)
  {
    reply = "{\"ok\":false,\"error\":" + json::quote(e.what()) + "}";
  }
  catch (...)
  {
    reply = "{\"ok\":false,\"error\":\"an unknown error\"}";
  }
  std::fputs(reply.c_str(), reply_out);
  std::fputc('\n', reply_out);
  std::fflush(reply_out);
  std::fclose(reply_out);
  return 0;
}
