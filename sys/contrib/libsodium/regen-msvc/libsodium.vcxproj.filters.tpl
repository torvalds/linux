<?xml version="1.0" encoding="utf-8"?>
<Project ToolsVersion="4.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
    <None Include="..\..\..\..\packaging\nuget\package.bat">
      <Filter>packaging</Filter>
    </None>
    <None Include="..\..\..\..\packaging\nuget\package.gsl">
      <Filter>packaging</Filter>
    </None>
    <None Include="..\..\..\..\packaging\nuget\package.nuspec">
      <Filter>packaging</Filter>
    </None>
    <None Include="..\..\..\..\packaging\nuget\package.targets">
      <Filter>packaging</Filter>
    </None>
    <None Include="..\..\..\..\packaging\nuget\package.config">
      <Filter>packaging</Filter>
    </None>
    <Xml Include="..\..\..\..\packaging\nuget\package.xml">
      <Filter>packaging</Filter>
    </Xml>
  </ItemGroup>
  <ItemGroup>
    <ResourceCompile Include="..\..\resource.rc" />
  </ItemGroup>
  <ItemGroup>
    {{f1}}
  </ItemGroup>
  <ItemGroup>
    {{f2}}
  </ItemGroup>
  <ItemGroup>
    {{fd}}
  </ItemGroup>
</Project>
