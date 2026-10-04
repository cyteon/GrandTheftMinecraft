// gtmpack build: one textured 1 m cube prop per Minecraft block, packed as an add-on DLC (dlc.rpf).
//
// Layout (mirrors Rockstar's own prop DLCs, e.g. mpbusiness2):
//   dlc.rpf/content.xml, setup2.xml
//   dlc.rpf/x64/levels/gta5/props/gtm_blocks.rpf/{gtm_<block>.ydr..., gtm_blocks.ytyp}
// content.xml registers the inner RPF (RPF_FILE) and requests its archetypes (DLC_ITYP_REQUEST .ityp).
using System.Globalization;
using System.Text;
using System.Xml;
using CodeWalker.GameFiles;

static class Build
{
    const string Device = "dlc_gtm";
    const string PropsRpf = "gtm_blocks";
    static readonly CultureInfo C = CultureInfo.InvariantCulture;
    static string F(float v) => v.ToString("0.######", C);

    record BlockDef(string Name, string Shader, int Material);

    // Faces of the unit cube: origin (top-left seen from outside), right and down axes, normal, sheet column.
    // Sheet columns: 0 top, 1 side, 2 bottom (each 128 px of the 512 px sheet).
    static readonly (float[] o, float[] u, float[] v, float[] n, int col)[] Faces =
    {
        (new float[] { 0, 1, 1 }, new float[] { 1, 0, 0 }, new float[] { 0, -1, 0 }, new float[] { 0, 0, 1 }, 0),
        (new float[] { 0, 0, 0 }, new float[] { 1, 0, 0 }, new float[] { 0, 1, 0 }, new float[] { 0, 0, -1 }, 2),
        (new float[] { 1, 1, 1 }, new float[] { -1, 0, 0 }, new float[] { 0, 0, -1 }, new float[] { 0, 1, 0 }, 1),
        (new float[] { 0, 0, 1 }, new float[] { 1, 0, 0 }, new float[] { 0, 0, -1 }, new float[] { 0, -1, 0 }, 1),
        (new float[] { 1, 0, 1 }, new float[] { 0, 1, 0 }, new float[] { 0, 0, -1 }, new float[] { 1, 0, 0 }, 1),
        (new float[] { 0, 1, 1 }, new float[] { 0, -1, 0 }, new float[] { 0, 0, -1 }, new float[] { -1, 0, 0 }, 1),
    };

    static string DrawableXml(BlockDef b)
    {
        string model = "gtm_" + b.Name;
        int bucket = b.Shader == "alpha" ? 1 : b.Shader == "cutout" ? 3 : 0;
        var vb = new StringBuilder();
        var ib = new StringBuilder();
        int vi = 0;
        const float iu = 0.5f / 512, iv = 0.5f / 128; // half-texel inset: no bleeding between sheet columns
        foreach (var f in Faces)
        {
            float u0 = f.col * 0.25f + iu, u1 = (f.col + 1) * 0.25f - iu, v0 = iv, v1 = 1 - iv;
            var corners = new (float a, float b, float uu, float vv)[] { (0, 0, u0, v0), (1, 0, u1, v0), (1, 1, u1, v1), (0, 1, u0, v1) };
            foreach (var c in corners)
            {
                float x = f.o[0] + f.u[0] * c.a + f.v[0] * c.b - 0.5f;
                float y = f.o[1] + f.u[1] * c.a + f.v[1] * c.b - 0.5f;
                float z = f.o[2] + f.u[2] * c.a + f.v[2] * c.b - 0.5f;
                vb.Append($"       {F(x)} {F(y)} {F(z)}   {F(f.n[0])} {F(f.n[1])} {F(f.n[2])}   255 255 255 255   {F(c.uu)} {F(c.vv)}\n");
            }
            // GTA's front faces wind counter-clockwise seen from outside: TL, BL, BR / BR, TR, TL
            ib.Append($"{vi} {vi + 3} {vi + 2} {vi + 2} {vi + 1} {vi} ");
            vi += 4;
        }
        return $@"<?xml version=""1.0"" encoding=""UTF-8""?>
<Drawable>
 <Name>{model}</Name>
 <BoundingSphereCenter x=""0"" y=""0"" z=""0"" />
 <BoundingSphereRadius value=""0.866026"" />
 <BoundingBoxMin x=""-0.5"" y=""-0.5"" z=""-0.5"" />
 <BoundingBoxMax x=""0.5"" y=""0.5"" z=""0.5"" />
 <LodDistHigh value=""9998"" />
 <LodDistMed value=""9998"" />
 <LodDistLow value=""9998"" />
 <LodDistVlow value=""9998"" />
 <FlagsHigh value=""1"" />
 <FlagsMed value=""0"" />
 <FlagsLow value=""0"" />
 <FlagsVlow value=""0"" />
 <ShaderGroup>
  <TextureDictionary>
   <Item>
    <Name>{model}</Name>
    <Unk32 value=""128"" />
    <Usage>DEFAULT</Usage>
    <UsageFlags>UNK24</UsageFlags>
    <ExtraFlags value=""0"" />
    <Width value=""512"" />
    <Height value=""128"" />
    <MipLevels value=""10"" />
    <Format>D3DFMT_A8R8G8B8</Format>
    <FileName>{model}.dds</FileName>
   </Item>
  </TextureDictionary>
  <Shaders>
   <Item>
    <Name>{b.Shader}</Name>
    <FileName>{b.Shader}.sps</FileName>
    <RenderBucket value=""{bucket}"" />
    <Parameters>
     <Item name=""DiffuseSampler"" type=""Texture"">
      <Name>{model}</Name>
     </Item>
     <Item name=""matMaterialColorScale"" type=""Vector"" x=""1"" y=""0"" z=""0"" w=""1"" />
     <Item name=""HardAlphaBlend"" type=""Vector"" x=""1"" y=""0"" z=""0"" w=""0"" />
    </Parameters>
   </Item>
  </Shaders>
 </ShaderGroup>
 <Skeleton>
  <Unknown1C value=""0"" />
  <Unknown50 value=""3283568608"" />
  <Unknown54 value=""3954038922"" />
  <Unknown58 value=""3624623659"" />
  <Bones>
   <Item>
    <Name>{model}</Name>
    <Tag value=""0"" />
    <Index value=""0"" />
    <ParentIndex value=""-1"" />
    <SiblingIndex value=""-1"" />
    <Flags>RotX, RotY, RotZ, TransX, TransY, TransZ</Flags>
    <Translation x=""0"" y=""0"" z=""0"" />
    <Rotation x=""0"" y=""0"" z=""0"" w=""1"" />
    <Scale x=""1"" y=""1"" z=""1"" />
    <TransformUnk x=""0"" y=""4"" z=""-3"" w=""0"" />
   </Item>
  </Bones>
 </Skeleton>
 <DrawableModelsHigh>
  <Item>
   <RenderMask value=""255"" />
   <Flags value=""0"" />
   <HasSkin value=""0"" />
   <BoneIndex value=""0"" />
   <Unknown1 value=""0"" />
   <Geometries>
    <Item>
     <ShaderIndex value=""0"" />
     <BoundingBoxMin x=""-0.5"" y=""-0.5"" z=""-0.5"" w=""-0.5"" />
     <BoundingBoxMax x=""0.5"" y=""0.5"" z=""0.5"" w=""0.5"" />
     <VertexBuffer>
      <Flags value=""0"" />
      <Layout type=""GTAV1"">
       <Position />
       <Normal />
       <Colour0 />
       <TexCoord0 />
      </Layout>
      <Data>
{vb}      </Data>
     </VertexBuffer>
     <IndexBuffer>
      <Data>
       {ib}
      </Data>
     </IndexBuffer>
    </Item>
   </Geometries>
  </Item>
 </DrawableModelsHigh>
 <Bounds type=""Composite"">
  <BoxMin x=""-0.5"" y=""-0.5"" z=""-0.5"" />
  <BoxMax x=""0.5"" y=""0.5"" z=""0.5"" />
  <BoxCenter x=""0"" y=""0"" z=""0"" />
  <SphereCenter x=""0"" y=""0"" z=""0"" />
  <SphereRadius value=""0.866026"" />
  <Margin value=""0"" />
  <Volume value=""1"" />
  <Inertia x=""0.166667"" y=""0.166667"" z=""0.166667"" />
  <MaterialIndex value=""0"" />
  <MaterialColourIndex value=""0"" />
  <ProceduralID value=""0"" />
  <RoomID value=""0"" />
  <PedDensity value=""0"" />
  <UnkFlags value=""0"" />
  <PolyFlags value=""0"" />
  <UnkType value=""1"" />
  <Children>
   <Item type=""Box"">
    <BoxMin x=""-0.5"" y=""-0.5"" z=""-0.5"" />
    <BoxMax x=""0.5"" y=""0.5"" z=""0.5"" />
    <BoxCenter x=""0"" y=""0"" z=""0"" />
    <SphereCenter x=""0"" y=""0"" z=""0"" />
    <SphereRadius value=""0.866026"" />
    <Margin value=""0.04"" />
    <Volume value=""1"" />
    <Inertia x=""0.166667"" y=""0.166667"" z=""0.166667"" />
    <MaterialIndex value=""{b.Material}"" />
    <MaterialColourIndex value=""0"" />
    <ProceduralID value=""0"" />
    <RoomID value=""0"" />
    <PedDensity value=""0"" />
    <UnkFlags value=""0"" />
    <PolyFlags value=""0"" />
    <UnkType value=""1"" />
    <CompositeTransform>
     1 0 0 0
     0 1 0 0
     0 0 1 0
     0 0 0 1
    </CompositeTransform>
    <CompositeFlags1>MAP_WEAPON, MAP_DYNAMIC, MAP_ANIMAL, MAP_COVER, MAP_VEHICLE</CompositeFlags1>
    <CompositeFlags2>VEHICLE_NOT_BVH, VEHICLE_BVH, PED, RAGDOLL, ANIMAL, ANIMAL_RAGDOLL, OBJECT, PLANT, PROJECTILE, EXPLOSION, FORKLIFT_FORKS, TEST_WEAPON, TEST_CAMERA, TEST_AI, TEST_SCRIPT, TEST_VEHICLE_WHEEL, GLASS</CompositeFlags2>
   </Item>
  </Children>
 </Bounds>
 <Lights />
</Drawable>";
    }

    static string YtypXml(IEnumerable<BlockDef> blocks)
    {
        var sb = new StringBuilder();
        sb.Append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<CMapTypes>\n <extensions />\n <archetypes>\n");
        foreach (var b in blocks)
        {
            string model = "gtm_" + b.Name;
            sb.Append($@"  <Item type=""CBaseArchetypeDef"">
   <lodDist value=""300"" />
   <flags value=""537001984"" />
   <specialAttribute value=""0"" />
   <bbMin x=""-0.5"" y=""-0.5"" z=""-0.5"" />
   <bbMax x=""0.5"" y=""0.5"" z=""0.5"" />
   <bsCentre x=""0"" y=""0"" z=""0"" />
   <bsRadius value=""0.866026"" />
   <hdTextureDist value=""100"" />
   <name>{model}</name>
   <textureDictionary />
   <clipDictionary />
   <drawableDictionary />
   <physicsDictionary>{PropsRpf}</physicsDictionary>
   <assetType>ASSET_TYPE_DRAWABLE</assetType>
   <assetName>{model}</assetName>
   <extensions />
  </Item>
");
        }
        sb.Append($" </archetypes>\n <name>{PropsRpf}</name>\n <dependencies />\n <compositeEntityTypes itemType=\"CCompositeEntityType\" />\n</CMapTypes>\n");
        return sb.ToString();
    }

    static string ContentXml() => $@"<?xml version=""1.0"" encoding=""UTF-8""?>
<CDataFileMgr__ContentsOfDataFileXml>
  <disabledFiles />
  <includedXmlFiles />
  <includedDataFiles />
  <dataFiles>
    <Item>
      <filename>{Device}:/%PLATFORM%/levels/gta5/props/{PropsRpf}.rpf</filename>
      <fileType>RPF_FILE</fileType>
      <overlay value=""false"" />
      <disabled value=""true"" />
      <persistent value=""true"" />
    </Item>
    <Item>
      <filename>{Device}:/%PLATFORM%/levels/gta5/props/{PropsRpf}.ityp</filename>
      <fileType>DLC_ITYP_REQUEST</fileType>
      <overlay value=""false"" />
      <disabled value=""true"" />
      <persistent value=""false"" />
      <contents>CONTENTS_PROPS</contents>
    </Item>
  </dataFiles>
  <contentChangeSets>
    <Item>
      <changeSetName>GTM_AUTOGEN</changeSetName>
      <filesToInvalidate />
      <filesToDisable />
      <filesToEnable>
        <Item>{Device}:/%PLATFORM%/levels/gta5/props/{PropsRpf}.rpf</Item>
        <Item>{Device}:/%PLATFORM%/levels/gta5/props/{PropsRpf}.ityp</Item>
      </filesToEnable>
      <txdToLoad />
      <txdToUnload />
      <residentResources />
      <unregisterResources />
    </Item>
  </contentChangeSets>
  <patchFiles />
</CDataFileMgr__ContentsOfDataFileXml>
";

    static string Setup2Xml() => $@"<?xml version=""1.0"" encoding=""UTF-8""?>
<SSetupData>
  <deviceName>{Device}</deviceName>
  <datFile>content.xml</datFile>
  <timeStamp>10/04/2026 12:00:00</timeStamp>
  <nameHash>gtm</nameHash>
  <contentChangeSetGroups>
    <Item>
      <NameHash>GROUP_STARTUP</NameHash>
      <ContentChangeSets>
        <Item>GTM_AUTOGEN</Item>
      </ContentChangeSets>
    </Item>
  </contentChangeSetGroups>
  <type>EXTRACONTENT_COMPAT_PACK</type>
  <order value=""90"" />
</SSetupData>
";

    public static int Run(string gameDir, string srcDir, string outRpf)
    {
        GTA5Keys.LoadFromPath(gameDir);
        var blocks = File.ReadAllLines(Path.Combine(srcDir, "blocks.txt"))
            .Where(l => l.Contains(';'))
            .Select(l => l.Split(';'))
            .Select(p => new BlockDef(p[0], p[1], int.Parse(p[2])))
            .ToList();

        var files = new List<(string name, byte[] data)>();
        foreach (var b in blocks)
        {
            string xml = DrawableXml(b);
            var doc = new XmlDocument();
            doc.LoadXml(xml);
            byte[] ydr = XmlMeta.GetData(doc, MetaFormat.Ydr, srcDir);
            if (ydr == null || ydr.Length < 16) throw new Exception($"ydr build failed for {b.Name}");
            files.Add(($"gtm_{b.Name}.ydr", ydr));
        }
        var ytypDoc = new XmlDocument();
        ytypDoc.LoadXml(YtypXml(blocks));
        byte[] ytyp = XmlMeta.GetData(ytypDoc, MetaFormat.RSC, srcDir);
        files.Add(($"{PropsRpf}.ytyp", ytyp));
        Console.WriteLine($"built {blocks.Count} drawables + {PropsRpf}.ytyp ({ytyp.Length} bytes)");

        string dir = Path.GetDirectoryName(Path.GetFullPath(outRpf));
        Directory.CreateDirectory(dir);
        if (File.Exists(outRpf)) File.Delete(outRpf);
        var root = RpfFile.CreateNew(dir, Path.GetFileName(outRpf), RpfEncryption.OPEN);
        RpfFile.CreateFile(root.Root, "content.xml", Encoding.UTF8.GetBytes(ContentXml()));
        RpfFile.CreateFile(root.Root, "setup2.xml", Encoding.UTF8.GetBytes(Setup2Xml()));
        var d = RpfFile.CreateDirectory(root.Root, "x64");
        d = RpfFile.CreateDirectory(d, "levels");
        d = RpfFile.CreateDirectory(d, "gta5");
        d = RpfFile.CreateDirectory(d, "props");
        var props = RpfFile.CreateNew(d, PropsRpf + ".rpf", RpfEncryption.OPEN);
        foreach (var (name, data) in files)
            RpfFile.CreateFile(props.Root, name, data);
        Console.WriteLine($"wrote {outRpf} ({new FileInfo(outRpf).Length / 1024} KB)");
        return 0;
    }

    // Re-open the built pack and dump one drawable back to XML (round-trip check).
    public static int Verify(string gameDir, string rpfPath, string outDir)
    {
        GTA5Keys.LoadFromPath(gameDir);
        var rpf = new RpfFile(rpfPath, Path.GetFileName(rpfPath));
        rpf.ScanStructure(s => { }, e => Console.Error.WriteLine(e));
        foreach (var e in rpf.AllEntries)
            Console.WriteLine($"  {e.Path}");
        var all = rpf.AllEntries.Concat(rpf.Children?.SelectMany(c => c.AllEntries) ?? Enumerable.Empty<RpfEntry>()).OfType<RpfFileEntry>().ToList();
        foreach (var e in rpf.Children ?? new List<RpfFile>())
            foreach (var ce in e.AllEntries)
                Console.WriteLine($"  {ce.Path}");
        Directory.CreateDirectory(outDir);
        foreach (var name in new[] { "gtm_grass_block.ydr", PropsRpf + ".ytyp", "content.xml" })
        {
            var fe = all.FirstOrDefault(x => x.NameLower == name);
            if (fe == null) { Console.Error.WriteLine($"missing {name}"); return 1; }
            var data = fe.File.ExtractFile(fe);
            string xml = name.EndsWith(".xml") ? Encoding.UTF8.GetString(data) : MetaXml.GetXml(fe, data, out _, outDir);
            File.WriteAllText(Path.Combine(outDir, name + ".xml"), xml);
            Console.WriteLine($"verified {name}: {data.Length} bytes -> {xml.Length} chars of XML");
        }
        return 0;
    }
}
