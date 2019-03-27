<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup Label="Globals">
    <ProjectGuid>{A185B162-6CB6-4502-B03F-B56F7699A8D9}</ProjectGuid>
    <ProjectName>libsodium</ProjectName>
    <PlatformToolset>{{platform}}</PlatformToolset>
  </PropertyGroup>
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="DebugDLL|Win32">
      <Configuration>DebugDLL</Configuration>
      <Platform>Win32</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="ReleaseDLL|Win32">
      <Configuration>ReleaseDLL</Configuration>
      <Platform>Win32</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="DebugDLL|x64">
      <Configuration>DebugDLL</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="ReleaseDLL|x64">
      <Configuration>ReleaseDLL</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="DebugLTCG|Win32">
      <Configuration>DebugLTCG</Configuration>
      <Platform>Win32</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="ReleaseLTCG|Win32">
      <Configuration>ReleaseLTCG</Configuration>
      <Platform>Win32</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="DebugLTCG|x64">
      <Configuration>DebugLTCG</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="ReleaseLTCG|x64">
      <Configuration>ReleaseLTCG</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="DebugLIB|Win32">
      <Configuration>DebugLIB</Configuration>
      <Platform>Win32</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="ReleaseLIB|Win32">
      <Configuration>ReleaseLIB</Configuration>
      <Platform>Win32</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="DebugLIB|x64">
      <Configuration>DebugLIB</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="ReleaseLIB|x64">
      <Configuration>ReleaseLIB</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Configuration">
    <ConfigurationType Condition="$(Configuration.IndexOf('DLL')) == -1">StaticLibrary</ConfigurationType>
    <ConfigurationType Condition="$(Configuration.IndexOf('DLL')) != -1">DynamicLibrary</ConfigurationType>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <ImportGroup Label="PropertySheets">
    <Import Project="$(ProjectDir)..\..\properties\$(Configuration).props" />
    <Import Project="$(ProjectDir)..\..\properties\Output.props" />
    <Import Project="$(ProjectDir)$(ProjectName).props" />
  </ImportGroup>
  <ItemGroup>
    <None Include="..\..\..\..\packaging\nuget\package.bat" />
    <None Include="..\..\..\..\packaging\nuget\package.config" />
    <None Include="..\..\..\..\packaging\nuget\package.gsl" />
    <None Include="..\..\..\..\packaging\nuget\package.nuspec" />
    <None Include="..\..\..\..\packaging\nuget\package.targets" />
  </ItemGroup>
  <ItemGroup>
    <Xml Include="..\..\..\..\packaging\nuget\package.xml" />
  </ItemGroup>
  <ItemGroup>
    {{v1}}
  </ItemGroup>
  <ItemGroup>
    {{v2}}
    <ClInclude Include="..\..\resource.h" />
  </ItemGroup>
  <ItemGroup>
    <ResourceCompile Include="..\..\resource.rc">
    </ResourceCompile>
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
  <ImportGroup Label="ExtensionTargets">
  </ImportGroup>
</Project>
