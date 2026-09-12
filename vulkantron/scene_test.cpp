#include "scene.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace fs = std::filesystem;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool close(float a, float b, float epsilon = 1e-4f) { return std::abs(a-b) < epsilon; }
void rejected(const std::function<void()>& run, const std::string& expected) {
    try { run(); }
    catch (const std::runtime_error& error) {
        if (std::string(error.what()).find(expected) != std::string::npos) return;
        throw std::runtime_error("Unexpected error instead of '" + expected + "': " + error.what());
    }
    throw std::runtime_error("Accepted invalid input: " + expected);
}
void write(const fs::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    if (!file) throw std::runtime_error("Could not write test fixture");
}
struct Fixture {
    fs::path root = fs::temp_directory_path() / ("vulkantron-scene-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() {
        require(fs::create_directory(root), "Fixture already exists");
        fs::create_directory(root/"data");
    }
    ~Fixture() { std::error_code ec; fs::remove_all(root,ec); }
    void obj(const std::string& text) { write(root/"data/lightcycle-high.obj",text); }
    void mtl(const std::string& text) { write(root/"data/lightcycle.mtl",text); }
};
VTSnapshot state() {
    VTSnapshot s{};
    s.grid_size = 40;
    s.players[0].eye[1] = -10;
    s.players[0].eye[2] = 5;
    s.players[0].target[2] = 5;
    for (int i = 0; i < 4; ++i) {
        s.players[i].direction = i;
        s.players[i].x = 5+10*i;
        s.players[i].y = 7;
        s.players[i].alive = 1;
    }
    return s;
}
std::array<float,3> ndc(const vt::Frame& frame, std::array<float,3> p) {
    float clip[4]{};
    for (int r = 0; r < 4; ++r) {
        clip[r] = frame.view_projection[12+r];
        for (int c = 0; c < 3; ++c) clip[r] += frame.view_projection[c*4+r]*p[c];
    }
    return {clip[0]/clip[3],clip[1]/clip[3],clip[2]/clip[3]};
}
const std::string vertices = "v 1 2 -1\nv -2 2 1\nv 1 -2 0\n";
const std::string valid = "mtllib lightcycle.mtl\n" + vertices + "usemtl Hull\nf 1 2 3";
void parser_and_frame() {
    Fixture f;
    f.mtl("newmtl Hull\nKd .2 .4 .6\n");
    f.obj(valid); // Final record deliberately has no newline.
    vt::Scene scene(f.root);
    require(scene.cycle_vertices() == 3,"Triangle expansion changed");
    auto snapshot = state();
    const VTTrail trails[] = {
        {{1,2},{3,2},3.5f,0}, {{3,2},{3,4},1.75f,2}, {{3,4},{5,4},0,3},
        {{1,1},{1,1},3.5f,1}
    };
    snapshot.trails = trails; snapshot.trail_count = 4;
    const auto before = snapshot;
    const auto frame = scene.frame(snapshot,2,false);
    require(std::memcmp(&before,&snapshot,sizeof(snapshot)) == 0,"Scene modified snapshot");
    require(frame.vertices.size() == 90,"Grid/trail/cycle triangle count changed");
    const auto begin_cycle = frame.vertices.size()-12;
    const float expected[4][2] = {{6,9},{17,6},{24,5},{33,8}};
    for (int i = 0; i < 4; ++i) {
        const auto& v = frame.vertices[begin_cycle+i*3];
        require(close(v.position[0],expected[i][0]) && close(v.position[1],expected[i][1]),
                "Classic heading or model scale changed");
        require(close(v.position[2],0),"Original half-bbox lift changed");
    }
    const auto& hull = frame.vertices[begin_cycle];
    require(close(hull.color[1]/hull.color[0],.55f) && close(hull.color[2]/hull.color[0],.14f),
            "Hull lost classic player color override");
    require(close(frame.vertices[66+2].position[2],3.5f) &&
            close(frame.vertices[72+2].position[2],1.75f),"Trail heights changed");
    require(close(frame.vertices[66].color[1],.85f),"Trail color changed");
    for (const auto& v : frame.vertices) {
        for (float x : v.position) require(std::isfinite(x),"Nonfinite output position");
        for (float x : v.color) require(std::isfinite(x),"Nonfinite output color");
        require(v.color[3] == 1,"Internal material marker leaked into output");
    }
    require(close(ndc(frame,{0,-9.5f,5})[2],0),"Vulkan near plane is not zero");
    require(close(ndc(frame,{0,250,5})[2],1),"Vulkan far plane is not one");
    require(ndc(frame,{0,0,6})[1] < 0,"Vulkan projection Y is not flipped");
    require(ndc(frame,{1,0,5})[0] > 0,"Camera X orientation changed");
    auto large = snapshot; large.grid_size = 720;
    const auto overview = scene.frame(large,16.0f/9,true);
    const float floor_depth = ndc(overview,{360,720,0})[2];
    const float line_depth = ndc(overview,{360,720,.015f})[2];
    require(floor_depth-line_depth > 1e-7f,"Overview loses floor-grid depth precision");
    rejected([&] { scene.frame(snapshot,0,false); },"positive aspect");
    rejected([&] { scene.frame(snapshot,std::numeric_limits<float>::denorm_min(),false); },"camera projection");
    auto bad_state = snapshot; bad_state.trail_count = 200001;
    rejected([&] { scene.frame(bad_state,1,false); },"rendering budget");
    bad_state = snapshot; bad_state.trails = nullptr;
    rejected([&] { scene.frame(bad_state,1,false); },"Missing classic trail");
    bad_state = snapshot; bad_state.players[0].direction = 4;
    rejected([&] { scene.frame(bad_state,1,false); },"direction");
    bad_state = snapshot; bad_state.players[0].eye[0] = std::numeric_limits<float>::quiet_NaN();
    rejected([&] { scene.frame(bad_state,1,false); },"camera eye");
    bad_state = snapshot;
    std::memcpy(bad_state.players[0].target,bad_state.players[0].eye,sizeof(bad_state.players[0].eye));
    rejected([&] { scene.frame(bad_state,1,false); },"direction");
    // Top-down camera uses the deterministic alternate up axis.
    bad_state = snapshot;
    bad_state.players[0].eye[1] = bad_state.players[0].target[1] = 0;
    bad_state.players[0].target[2] = 0;
    scene.frame(bad_state,1,false);

    auto bad_obj = [&](const std::string& text, const char* error) {
        f.obj(text); rejected([&] { vt::Scene bad(f.root); },error);
    };
    bad_obj(vertices+"f 0 2 3\n","invalid OBJ index");
    bad_obj(vertices+"f 1 2 4\n","out of range");
    bad_obj(vertices+"f -4 -2 -1\n","out of range");
    bad_obj(vertices+"f 1 2 9999999999999999999999\n","invalid OBJ index");
    bad_obj(vertices+"f 1/1 2/1 3/1\n","out of range");
    bad_obj(vertices+"vn 0 0 1\nf 1//1 2//1 3//2\n","out of range");
    bad_obj(vertices+"f 1///1 2 3\n","components");
    bad_obj("v nan 0 0\nf 1 1 1\n","numeric");
    bad_obj("v 1e100 0 0\nf 1 1 1\n","numeric");
    bad_obj(vertices+"usemtl Unknown\nf 1 2 3\n","unknown material");
    bad_obj("mtllib ../outside.mtl\n"+vertices+"f 1 2 3\n","beside the OBJ");
    bad_obj("mtllib missing.mtl\n"+vertices+"f 1 2 3\n","cannot open");
    bad_obj(std::string(5000,' ') + "\n" + vertices+"f 1 2 3\n","record exceeds");
    std::string too_many = vertices+"f";
    for (int i = 0; i < 65; ++i) too_many += " 1";
    bad_obj(too_many,"64 corners");
    bad_obj("v 0 0 0\nv 2 0 0\nv .5 .5 0\nv 0 2 0\nf 1 2 3 4\n","nonconvex");
    bad_obj("v 0 0 0\nv 2 0 0\nv 2 2 1\nv 0 2 0\nf 1 2 3 4\n","nonplanar");
    bad_obj(std::string("v 0\0 0 0\n",10),"NUL");
    f.obj(valid); f.mtl("newmtl Hull\nKd -1 0 0\n");
    rejected([&] { vt::Scene bad(f.root); },"negative diffuse");
    f.mtl("newmtl Hull\nKd 1 0 nan\n");
    rejected([&] { vt::Scene bad(f.root); },"numeric");
    f.mtl("newmtl Hull\nnewmtl Hull\n");
    rejected([&] { vt::Scene bad(f.root); },"duplicate material");
    // Valid relative indices, UV and normal references, and convex fan support.
    f.obj("v 0 0 0\nv 2 0 0\nv 2 2 0\nv 0 2 0\nvt 0 0\nvn 0 0 1\nf -4/1/1 -3/1/1 -2/1/1 -1/1/1\n");
    require(vt::Scene(f.root).cycle_vertices() == 6,"Valid convex OBJ failed");
    // Per-face material survives group changes rather than becoming Hull.
    f.mtl("newmtl Hull\nKd .2 .4 .6\nnewmtl Other\nKd .25 .5 .75\n");
    f.obj(valid+"\nusemtl Other\ng next\nf 1 2 3\n");
    vt::Scene grouped(f.root);
    const auto grouped_frame = grouped.frame(snapshot,1,true);
    const auto& other = grouped_frame.vertices[grouped_frame.vertices.size()-3];
    require(close(other.color[1]/other.color[0],2) && close(other.color[2]/other.color[0],3),
            "Per-face diffuse material changed");
}
void original_geometry(const fs::path& root) {
    vt::Scene scene(root);
    require(scene.cycle_vertices() == 3567,"Original high-detail cycle vertex count changed");
    auto snapshot = state();
    snapshot.players[0].x = snapshot.players[0].y = 0;
    snapshot.players[0].direction = 0;
    for (int i = 1; i < 4; ++i) snapshot.players[i].alive = 0;
    const auto frame = scene.frame(snapshot,16.0f/9,true);
    std::uint64_t hash = 1469598103934665603ULL;
    float minimum = 1e10f, maximum = -minimum;
    for (std::size_t i = frame.vertices.size()-scene.cycle_vertices(); i < frame.vertices.size(); ++i) {
        const auto& v = frame.vertices[i];
        minimum = std::min(minimum,v.position[2]); maximum = std::max(maximum,v.position[2]);
        for (float p : v.position) {
            std::uint32_t bits; std::memcpy(&bits,&p,sizeof(bits));
            for (int shift = 0; shift < 32; shift += 8) { hash ^= (bits >> shift)&255; hash *= 1099511628211ULL; }
        }
    }
    require(close(minimum,0) && close(maximum,4.1008f),"Original model bounds/ground lift changed");
    require(hash == 0x61a743ae36aa759fULL,"Original OBJ position bytes changed");
    std::cout << "VT_SCENE_ORIGINAL vertices=" << scene.cycle_vertices() << " position_hash=" << std::hex << hash << std::dec << '\n';
}
}
int main(int argc, char** argv) {
    try {
        require(argc == 2,"Usage: scene_test ASSET_ROOT");
        parser_and_frame(); original_geometry(argv[1]);
        std::cout << "VT_SCENE_TEST_OK parser_bounds materials headings trails projection budget\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "VT_SCENE_TEST_FAILED: " << error.what() << '\n';
        return 1;
    }
}
