#include "renderer.hpp"
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include "scene_vs.h"
#include "scene_ps.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace platz::engine {
using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
constexpr UINT bufferCount = 2;
constexpr float fov = XMConvertToRadians(47.f);
constexpr DXGI_FORMAT colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT tokenFormat = DXGI_FORMAT_R32_UINT;
constexpr DXGI_FORMAT depthFormat = DXGI_FORMAT_D32_FLOAT;

void check(HRESULT result, const char* operation) {
    if (SUCCEEDED(result)) return;
    std::ostringstream message;
    message << operation << " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ").";
    throw std::runtime_error(message.str());
}

XMVECTOR vector(Vec3 v) { return XMVectorSet(v[0], v[1], v[2], 0); }
XMVECTOR offset(const Camera& camera) {
    float cp = std::cos(camera.pitch);
    return XMVectorSet(std::sin(camera.yaw) * cp, std::sin(camera.pitch), std::cos(camera.yaw) * cp, 0);
}

D3D12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES value{};
    value.Type = type;
    value.CreationNodeMask = value.VisibleNodeMask = 1;
    return value;
}
D3D12_RESOURCE_DESC bufferDesc(UINT64 bytes) {
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = d.DepthOrArraySize = d.MipLevels = d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return d;
}
ComPtr<ID3D12Resource> makeBuffer(ID3D12Device* device, UINT64 bytes, D3D12_HEAP_TYPE type,
                                  D3D12_RESOURCE_STATES state) {
    auto properties = heap(type);
    auto desc = bufferDesc(bytes);
    ComPtr<ID3D12Resource> result;
    check(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                          IID_PPV_ARGS(&result)), "Allocating GPU buffer");
    return result;
}
void transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after};
    list->ResourceBarrier(1, &b);
}

struct Vertex { XMFLOAT3 position, normal; };
struct MeshRange { UINT startIndex, count; };
struct Geometry {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::array<MeshRange, 4> ranges{};
    UINT vertex(XMFLOAT3 p, XMFLOAT3 n) {
        vertices.push_back({p, n});
        return static_cast<UINT>(vertices.size() - 1);
    }
    void quad(UINT a, UINT b, UINT c, UINT d) {
        indices.insert(indices.end(), {a, b, c, a, c, d});
    }
    void finish(unsigned slot, UINT start) {
        ranges[slot] = {start, static_cast<UINT>(indices.size()) - start};
    }
};

Geometry makeGeometry() {
    Geometry g;
    // Separate face normals give the unit cube sharp edges under nonuniform scale.
    for (unsigned axis = 0; axis < 3; ++axis) {
        for (float sign : {-1.f, 1.f}) {
            UINT first = static_cast<UINT>(g.vertices.size());
            for (auto corner : std::array<std::array<float,2>,4>{{{-1,-1},{1,-1},{1,1},{-1,1}}}) {
                Vec3 p{}, n{};
                p[axis] = sign * .5f; n[axis] = sign;
                p[(axis+1)%3] = corner[0] * .5f;
                p[(axis+2)%3] = corner[1] * .5f;
                g.vertex({p[0],p[1],p[2]}, {n[0],n[1],n[2]});
            }
            g.quad(first, first+1, first+2, first+3);
        }
    }
    g.finish(0, 0);
    constexpr unsigned rings = 32, segments = 64;
    UINT start = static_cast<UINT>(g.indices.size());
    UINT base = static_cast<UINT>(g.vertices.size());
    for (unsigned y = 0; y <= rings; ++y) {
        float theta = XM_PI * static_cast<float>(y) / rings;
        for (unsigned x = 0; x <= segments; ++x) {
            float phi = XM_2PI * static_cast<float>(x) / segments;
            XMFLOAT3 n{std::sin(theta)*std::cos(phi), std::cos(theta), std::sin(theta)*std::sin(phi)};
            g.vertex({n.x*.5f,n.y*.5f,n.z*.5f}, n);
            if (x && y) {
                UINT i = base + y*(segments+1)+x;
                g.quad(i-segments-2, i-1, i, i-segments-1);
            }
        }
    }
    g.finish(1, start);
    start = static_cast<UINT>(g.indices.size());
    for (unsigned i = 0; i < segments; ++i) {
        float a = XM_2PI * i / segments, b = XM_2PI * (i+1) / segments;
        XMFLOAT3 n1{std::cos(a),0,std::sin(a)}, n2{std::cos(b),0,std::sin(b)};
        XMFLOAT3 bottom1{n1.x*.5f,-.5f,n1.z*.5f}, top1{bottom1.x,.5f,bottom1.z};
        XMFLOAT3 bottom2{n2.x*.5f,-.5f,n2.z*.5f}, top2{bottom2.x,.5f,bottom2.z};
        UINT p1=g.vertex(bottom1,n1), p2=g.vertex(top1,n1), p3=g.vertex(top2,n2), p4=g.vertex(bottom2,n2);
        g.quad(p1,p2,p3,p4);
        for (float y : {-.5f,.5f}) {
            XMFLOAT3 n{0,y*2,0};
            UINT center=g.vertex({0,y,0},n);
            UINT edge1=g.vertex({bottom1.x,y,bottom1.z},n), edge2=g.vertex({bottom2.x,y,bottom2.z},n);
            g.indices.insert(g.indices.end(), {center,edge1,edge2});
        }
    }
    g.finish(2, start);
    start = static_cast<UINT>(g.indices.size());
    base = static_cast<UINT>(g.vertices.size());
    for (auto p : std::array<XMFLOAT3,4>{{{-1,0,-1},{1,0,-1},{1,0,1},{-1,0,1}}})
        g.vertex(p, {0,1,0});
    g.quad(base,base+1,base+2,base+3);
    g.finish(3,start);
    return g;
}

struct Constants {
    XMFLOAT4X4 viewProjection, world, normal;
    Vec3 color;
    std::uint32_t token;
    XMFLOAT3 eye;
    std::uint32_t selected;
    std::uint32_t mesh;
};
static_assert(sizeof(Constants) == 57 * 4);

std::string utf8(const wchar_t* value) {
    int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    if (!result.empty()) result.pop_back();
    return result;
}
}

void Camera::orbit(float dx, float dy) {
    yaw = std::remainder(yaw - dx*.006f, XM_2PI);
    pitch = std::clamp(pitch + dy*.006f, -1.50f, 1.50f);
}
void Camera::zoom(float steps) {
    distance = std::clamp(distance * std::exp(-steps*.14f), .22f, 650.f);
}
void Camera::move(float right, float up, float forward) {
    target[0] += std::cos(yaw)*right - std::sin(yaw)*forward;
    target[1] += up;
    target[2] += -std::sin(yaw)*right - std::cos(yaw)*forward;
}
void Camera::pan(float dx, float dy, float height) {
    float units = 2 * distance * std::tan(fov/2) / std::max(height, 1.f);
    move(-dx*units, dy*units*std::cos(pitch), dy*units*std::sin(pitch));
}
void Camera::frame(Vec3 center, Vec3 dimensions) {
    target = center;
    distance = std::clamp(XMVectorGetX(XMVector3Length(vector(dimensions))) * 1.45f, .5f, 650.f);
}

struct Renderer::Impl {
    struct Frame {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> back;
        UINT64 finished{};
    };
    std::array<Frame, bufferCount> frames;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swap;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12CommandAllocator> auxiliary;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    ComPtr<ID3D12DescriptorHeap> rtvs, dsv;
    ComPtr<ID3D12Resource> color, ids, depth, geometry;
    D3D12_VERTEX_BUFFER_VIEW vb{};
    D3D12_INDEX_BUFFER_VIEW ib{};
    std::array<MeshRange,4> ranges{};
    HANDLE event{};
    UINT64 serial{};
    UINT width{}, height{}, rtvStep{};
    std::string adapterName;
    bool drawn{};

    ~Impl() {
        if (queue && fence && event) {
            try { wait(signal()); } catch (...) {}
        }
        if (event) CloseHandle(event);
    }
    UINT64 signal() {
        check(queue->Signal(fence.Get(), ++serial), "Signaling the GPU");
        return serial;
    }
    void wait(UINT64 value) {
        auto completed = fence->GetCompletedValue();
        if (completed == UINT64_MAX) check(device->GetDeviceRemovedReason(), "GPU was removed");
        if (completed >= value) return;
        check(fence->SetEventOnCompletion(value, event), "Waiting for the GPU");
        if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0)
            throw std::runtime_error("The graphics adapter did not complete this operation within 10 seconds.");
        if (fence->GetCompletedValue() == UINT64_MAX) check(device->GetDeviceRemovedReason(), "GPU was removed");
    }
    void begin(ID3D12CommandAllocator* allocator) {
        check(allocator->Reset(), "Resetting GPU frame memory");
        check(list->Reset(allocator, nullptr), "Recording GPU commands");
    }
    void execute() {
        check(list->Close(), "Closing GPU commands");
        ID3D12CommandList* batch[] = {list.Get()};
        queue->ExecuteCommandLists(1, batch);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv(UINT index) const {
        auto result = rtvs->GetCPUDescriptorHandleForHeapStart();
        result.ptr += static_cast<SIZE_T>(index)*rtvStep;
        return result;
    }
    void initialize(HWND window);
    void makePipeline();
    void uploadGeometry();
    void targets(UINT w,UINT h);
    void draw(std::span<const DrawObject>, const Camera&, UINT);
    Capture read(UINT x,UINT y,UINT w,UINT h,bool withColor);
};

void Renderer::Impl::initialize(HWND window) {
    if (!IsWindow(window)) throw std::invalid_argument("The viewport requires a valid native window.");
    ComPtr<IDXGIFactory6> factory;
    check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "Opening DXGI");
    for (UINT i=0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        HRESULT result = factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                             IID_PPV_ARGS(&adapter));
        if (result == DXGI_ERROR_NOT_FOUND) break;
        check(result, "Enumerating hardware adapters");
        DXGI_ADAPTER_DESC1 desc{};
        check(adapter->GetDesc1(&desc), "Querying graphics adapter");
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) continue;
        D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_0};
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
            model.HighestShaderModel < D3D_SHADER_MODEL_6_0) {
            device.Reset();
            continue;
        }
        adapterName = utf8(desc.Description);
        break;
    }
    if (!device) throw std::runtime_error("PLATZ requires a hardware Direct3D 12 adapter with feature level 12.0 and Shader Model 6.0.");
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    check(device->CreateCommandQueue(&queueDesc,IID_PPV_ARGS(&queue)), "Creating graphics queue");
    for (auto& frame : frames)
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&frame.allocator)), "Allocating frame commands");
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&auxiliary)), "Allocating transfer commands");
    check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,auxiliary.Get(),nullptr,IID_PPV_ARGS(&list)), "Creating graphics command list");
    check(list->Close(), "Initializing command list");
    check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)), "Creating GPU fence");
    event = CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if (!event) throw std::runtime_error("Cannot create the GPU completion event.");

    RECT rect{};
    GetClientRect(window,&rect);
    width = std::max(1L,rect.right);
    height = std::max(1L,rect.bottom);
    DXGI_SWAP_CHAIN_DESC1 chain{};
    chain.Width=width; chain.Height=height; chain.Format=colorFormat; chain.SampleDesc.Count=1;
    chain.BufferCount=bufferCount; chain.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    chain.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> initial;
    check(factory->CreateSwapChainForHwnd(queue.Get(),window,&chain,nullptr,nullptr,&initial), "Creating viewport swapchain");
    check(initial.As(&swap), "Opening flip-model swapchain");
    check(factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER), "Configuring viewport");
    D3D12_DESCRIPTOR_HEAP_DESC descriptors{};
    descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; descriptors.NumDescriptors=2;
    check(device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&rtvs)), "Creating render-target descriptors");
    rtvStep=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    descriptors.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV; descriptors.NumDescriptors=1;
    check(device->CreateDescriptorHeap(&descriptors,IID_PPV_ARGS(&dsv)), "Creating depth descriptor");
    makePipeline();
    uploadGeometry();
    targets(width,height);
}

void Renderer::Impl::makePipeline() {
    D3D12_ROOT_PARAMETER param{};
    param.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    param.Constants.Num32BitValues=sizeof(Constants)/4;
    param.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC signature{};
    signature.NumParameters=1; signature.pParameters=&param;
    signature.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob, error;
    check(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error), "Serializing root signature");
    check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)), "Creating root signature");
    const D3D12_INPUT_ELEMENT_DESC input[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
    pso.pRootSignature=root.Get(); pso.InputLayout={input,2};
    pso.VS={scene_vs,sizeof(scene_vs)}; pso.PS={scene_ps,sizeof(scene_ps)};
    pso.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pso.RasterizerState.DepthClipEnable=TRUE;
    pso.BlendState.IndependentBlendEnable=TRUE;
    for (UINT i=0;i<2;++i) {
        auto& blend=pso.BlendState.RenderTarget[i];
        blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;
        blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;
        blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
        blend.LogicOp=D3D12_LOGIC_OP_NOOP;
        blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    }
    pso.DepthStencilState.DepthEnable=TRUE;
    pso.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
    pso.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
    pso.SampleMask=UINT_MAX; pso.SampleDesc.Count=1; pso.NumRenderTargets=2;
    pso.RTVFormats[0]=colorFormat; pso.RTVFormats[1]=tokenFormat; pso.DSVFormat=depthFormat;
    check(device->CreateGraphicsPipelineState(&pso,IID_PPV_ARGS(&pipeline)), "Compiling native graphics pipeline");
}

void Renderer::Impl::uploadGeometry() {
    auto source=makeGeometry();
    ranges=source.ranges;
    UINT vertexBytes=static_cast<UINT>(source.vertices.size()*sizeof(Vertex));
    UINT indexBytes=static_cast<UINT>(source.indices.size()*sizeof(UINT));
    UINT bytes=vertexBytes+indexBytes;
    auto upload=makeBuffer(device.Get(),bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
    geometry=makeBuffer(device.Get(),bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COPY_DEST);
    std::uint8_t* mapped{};
    D3D12_RANGE noRead{0,0};
    check(upload->Map(0,&noRead,reinterpret_cast<void**>(&mapped)), "Mapping mesh upload");
    std::memcpy(mapped,source.vertices.data(),vertexBytes);
    std::memcpy(mapped+vertexBytes,source.indices.data(),indexBytes);
    upload->Unmap(0,nullptr);
    begin(auxiliary.Get());
    list->CopyBufferRegion(geometry.Get(),0,upload.Get(),0,bytes);
    transition(list.Get(),geometry.Get(),D3D12_RESOURCE_STATE_COPY_DEST,
               D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER);
    execute(); wait(signal());
    vb={geometry->GetGPUVirtualAddress(),vertexBytes,sizeof(Vertex)};
    ib={vb.BufferLocation+vertexBytes,indexBytes,DXGI_FORMAT_R32_UINT};
}

void Renderer::Impl::targets(UINT w, UINT h) {
    wait(signal());
    for (auto& frame:frames) frame.back.Reset();
    color.Reset(); ids.Reset(); depth.Reset(); drawn=false;
    if (width!=w || height!=h)
        check(swap->ResizeBuffers(bufferCount,w,h,colorFormat,0), "Resizing presentation buffers");
    width=w; height=h;
    for (UINT i=0;i<bufferCount;++i)
        check(swap->GetBuffer(i,IID_PPV_ARGS(&frames[i].back)), "Acquiring presentation buffer");
    auto texture=[&](DXGI_FORMAT format,D3D12_RESOURCE_FLAGS flags,D3D12_RESOURCE_STATES state,D3D12_CLEAR_VALUE clear) {
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=w; desc.Height=h; desc.DepthOrArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
        desc.Format=format; desc.Flags=flags; clear.Format=format;
        auto properties=heap(D3D12_HEAP_TYPE_DEFAULT);
        ComPtr<ID3D12Resource> tex;
        check(device->CreateCommittedResource(&properties,D3D12_HEAP_FLAG_NONE,&desc,state,&clear,IID_PPV_ARGS(&tex)), "Allocating viewport attachment");
        return tex;
    };
    D3D12_CLEAR_VALUE clear{};
    clear.Color[0]=.100f; clear.Color[1]=.126f; clear.Color[2]=.155f; clear.Color[3]=1;
    color=texture(colorFormat,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET,clear);
    clear={};
    ids=texture(tokenFormat,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,D3D12_RESOURCE_STATE_RENDER_TARGET,clear);
    clear.DepthStencil.Depth=1;
    depth=texture(depthFormat,D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,D3D12_RESOURCE_STATE_DEPTH_WRITE,clear);
    device->CreateRenderTargetView(color.Get(),nullptr,rtv(0));
    device->CreateRenderTargetView(ids.Get(),nullptr,rtv(1));
    device->CreateDepthStencilView(depth.Get(),nullptr,dsv->GetCPUDescriptorHandleForHeapStart());
}

void Renderer::Impl::draw(std::span<const DrawObject> objects,const Camera& camera,UINT selected) {
    const UINT index=swap->GetCurrentBackBufferIndex();
    auto& frame=frames[index];
    wait(frame.finished);
    begin(frame.allocator.Get());
    list->SetPipelineState(pipeline.Get());
    list->SetGraphicsRootSignature(root.Get());
    D3D12_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
    D3D12_RECT scissor{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
    list->RSSetViewports(1,&viewport); list->RSSetScissorRects(1,&scissor);
    auto first=rtv(0), ds=dsv->GetCPUDescriptorHandleForHeapStart();
    list->OMSetRenderTargets(2,&first,TRUE,&ds);
    constexpr float background[]{.100f,.126f,.155f,1};
    constexpr float empty[]{0,0,0,0};
    list->ClearRenderTargetView(first,background,0,nullptr);
    list->ClearRenderTargetView(rtv(1),empty,0,nullptr);
    list->ClearDepthStencilView(ds,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0,1,&vb);
    list->IASetIndexBuffer(&ib);

    Constants c{};
    XMVECTOR center=vector(camera.target), eye=center+offset(camera)*camera.distance;
    auto view=XMMatrixLookAtRH(eye,center,XMVectorSet(0,1,0,0));
    auto projection=XMMatrixPerspectiveFovRH(fov,static_cast<float>(width)/static_cast<float>(height),.04f,2000.f);
    XMStoreFloat4x4(&c.viewProjection,view*projection);
    XMStoreFloat3(&c.eye,eye); c.selected=selected;
    auto submit=[&](unsigned kind,XMMATRIX world) {
        XMStoreFloat4x4(&c.world,world);
        XMStoreFloat4x4(&c.normal,XMMatrixTranspose(XMMatrixInverse(nullptr,world)));
        c.mesh=kind;
        list->SetGraphicsRoot32BitConstants(0,sizeof(c)/4,&c,0);
        list->DrawIndexedInstanced(ranges[kind].count,1,ranges[kind].startIndex,0,0);
    };
    submit(3,XMMatrixScaling(1800,1,1800)*XMMatrixTranslation(camera.target[0],-.012f,camera.target[2]));
    for (const auto& o:objects) {
        auto kind=static_cast<unsigned>(o.mesh);
        if (o.token==0 || kind>=3) continue;
        const auto& r=o.rotation;
        auto rotation=XMMatrixRotationX(XMConvertToRadians(r[0]))*XMMatrixRotationY(XMConvertToRadians(r[1]))*
                      XMMatrixRotationZ(XMConvertToRadians(r[2]));
        c.color=o.color; c.token=o.token;
        submit(kind,XMMatrixScalingFromVector(vector(o.scale))*rotation*XMMatrixTranslationFromVector(vector(o.position)));
    }
    transition(list.Get(),color.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
    transition(list.Get(),frame.back.Get(),D3D12_RESOURCE_STATE_PRESENT,D3D12_RESOURCE_STATE_COPY_DEST);
    list->CopyResource(frame.back.Get(),color.Get());
    transition(list.Get(),frame.back.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PRESENT);
    transition(list.Get(),color.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    execute();
    frame.finished=signal();
    check(swap->Present(1,0), "Presenting viewport");
    drawn=true;
}

Capture Renderer::Impl::read(UINT x,UINT y,UINT w,UINT h,bool withColor) {
    Capture result;
    if (!drawn || !w || !h) return result;
    // Both attachments use four bytes per pixel. Rows and planes obey D3D12 copy-footprint alignment.
    const UINT pitch=(w*4+255)&~255u;
    const UINT64 plane=(static_cast<UINT64>(pitch)*h+511)&~511ull;
    const UINT64 size=plane*(withColor ? 2 : 1);
    auto staging=makeBuffer(device.Get(),size,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
    begin(auxiliary.Get());
    D3D12_BOX box{x,y,0,x+w,y+h,1};
    auto copy=[&](ID3D12Resource* tex,DXGI_FORMAT format,UINT64 offsetBytes) {
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.pResource=tex;
        D3D12_TEXTURE_COPY_LOCATION dest{};
        dest.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dest.pResource=staging.Get();
        dest.PlacedFootprint.Offset=offsetBytes;
        dest.PlacedFootprint.Footprint={format,w,h,1,pitch};
        transition(list.Get(),tex,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyTextureRegion(&dest,0,0,0,&src,&box);
        transition(list.Get(),tex,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
    };
    copy(ids.Get(),tokenFormat,0);
    if (withColor) copy(color.Get(),colorFormat,plane);
    execute(); wait(signal());
    result.width=w; result.height=h;
    result.tokens.resize(static_cast<size_t>(w)*h);
    if (withColor) result.rgba.resize(static_cast<size_t>(w)*h*4);
    std::uint8_t* data{};
    D3D12_RANGE readRange{0,static_cast<SIZE_T>(size)};
    check(staging->Map(0,&readRange,reinterpret_cast<void**>(&data)), "Reading rendered viewport");
    for (UINT row=0;row<h;++row) {
        const auto dst=static_cast<size_t>(row)*w, src=static_cast<size_t>(row)*pitch;
        std::memcpy(result.tokens.data()+dst,data+src,w*4);
        if (withColor) std::memcpy(result.rgba.data()+dst*4,data+plane+src,w*4);
    }
    D3D12_RANGE noWrite{0,0};
    staging->Unmap(0,&noWrite);
    return result;
}

Renderer::Renderer(void* window):impl_(std::make_unique<Impl>()) {
    impl_->initialize(static_cast<HWND>(window));
}
Renderer::~Renderer() = default;
void Renderer::resize(unsigned width,unsigned height) {
    if (width && height && (width!=impl_->width || height!=impl_->height))
        impl_->targets(width,height);
}
void Renderer::draw(std::span<const DrawObject> objects,const Camera& camera,UINT selected) {
    impl_->draw(objects,camera,selected);
}
std::uint32_t Renderer::pick(int x,int y) {
    if (!impl_->drawn || x<0 || y<0 || static_cast<unsigned>(x)>=impl_->width || static_cast<unsigned>(y)>=impl_->height)
        return 0;
    return impl_->read(x,y,1,1,false).tokens.front();
}
Capture Renderer::capture() { return impl_->read(0,0,impl_->width,impl_->height,true); }
const std::string& Renderer::adapter() const { return impl_->adapterName; }
}
