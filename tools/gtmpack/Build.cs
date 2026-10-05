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

    const string TexDict = "gtm_tex";

    // models.txt row: model;texture;shader;material;collision half-extents ("-" = none)
    record ModelDef(string Name, string Texture, string Shader, int Material, float[] Half);

    class Geo
    {
        public List<float[]> V = new();
        public List<int> I = new();
        public float[] Min = { 1e9f, 1e9f, 1e9f }, Max = { -1e9f, -1e9f, -1e9f };
        public static Geo Load(string path)
        {
            var g = new Geo();
            foreach (var line in File.ReadAllLines(path))
            {
                var p = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (p.Length == 0) continue;
                if (p[0] == "v")
                {
                    var v = p.Skip(1).Select(x => float.Parse(x, C)).ToArray();
                    g.V.Add(v);
                    for (int k = 0; k < 3; k++) { g.Min[k] = Math.Min(g.Min[k], v[k]); g.Max[k] = Math.Max(g.Max[k], v[k]); }
                }
                else if (p[0] == "i")
                    g.I.AddRange(p.Skip(1).Select(int.Parse));
            }
            return g;
        }
        public float Radius => (float)Math.Sqrt(Enumerable.Range(0, 3).Sum(k => Math.Pow(Math.Max(Math.Abs(Min[k]), Math.Abs(Max[k])), 2)));
    }

    static (int w, int h, int mips) DdsInfo(string path)
    {
        using var br = new BinaryReader(File.OpenRead(path));
        br.ReadBytes(12);
        int h = br.ReadInt32(), w = br.ReadInt32();
        br.ReadBytes(8);
        int mips = br.ReadInt32();
        return (w, h, Math.Max(1, mips));
    }

    static string BoundsXml(ModelDef m)
    {
        if (m.Half == null) return "";
        float[] h = m.Half.Take(3).ToArray(), c = m.Half.Length >= 6 ? m.Half.Skip(3).Take(3).ToArray() : new float[3];
        string mn = $@"x=""{F(c[0] - h[0])}"" y=""{F(c[1] - h[1])}"" z=""{F(c[2] - h[2])}""";
        string mx = $@"x=""{F(c[0] + h[0])}"" y=""{F(c[1] + h[1])}"" z=""{F(c[2] + h[2])}""";
        string ctr = $@"x=""{F(c[0])}"" y=""{F(c[1])}"" z=""{F(c[2])}""";
        float r = (float)Math.Sqrt(h.Sum(v => v * v));
        float vol = 8 * h[0] * h[1] * h[2];
        float ix = (4 * (h[1] * h[1] + h[2] * h[2])) / 12, iy = (4 * (h[0] * h[0] + h[2] * h[2])) / 12, iz = (4 * (h[0] * h[0] + h[1] * h[1])) / 12;
        float margin = Math.Min(0.04f, h.Min() * 0.5f);
        string common(string mat) => $@"
  <BoxCenter {ctr} />
  <SphereCenter {ctr} />
  <SphereRadius value=""{F(r)}"" />";
        return $@" <Bounds type=""Composite"">
  <BoxMin {mn} />
  <BoxMax {mx} />{common("0")}
  <Margin value=""0"" />
  <Volume value=""{F(vol)}"" />
  <Inertia x=""{F(ix)}"" y=""{F(iy)}"" z=""{F(iz)}"" />
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
    <BoxMin {mn} />
    <BoxMax {mx} />
    <BoxCenter {ctr} />
    <SphereCenter {ctr} />
    <SphereRadius value=""{F(r)}"" />
    <Margin value=""{F(margin)}"" />
    <Volume value=""{F(vol)}"" />
    <Inertia x=""{F(ix)}"" y=""{F(iy)}"" z=""{F(iz)}"" />
    <MaterialIndex value=""{m.Material}"" />
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
";
    }

    static string DrawableXml(ModelDef m, Geo g)
    {
        int bucket = m.Shader == "alpha" ? 1 : m.Shader == "cutout" ? 3 : 0;
        var vb = new StringBuilder();
        foreach (var v in g.V)
            vb.Append($"       {F(v[0])} {F(v[1])} {F(v[2])}   {F(v[3])} {F(v[4])} {F(v[5])}   255 255 255 255   {F(v[6])} {F(v[7])}\n");
        var ib = new StringBuilder();
        for (int k = 0; k < g.I.Count; k++)
            ib.Append(g.I[k]).Append(k % 24 == 23 ? "\n       " : " ");
        string mn = $@"x=""{F(g.Min[0])}"" y=""{F(g.Min[1])}"" z=""{F(g.Min[2])}""", mx = $@"x=""{F(g.Max[0])}"" y=""{F(g.Max[1])}"" z=""{F(g.Max[2])}""";
        return $@"<?xml version=""1.0"" encoding=""UTF-8""?>
<Drawable>
 <Name>{m.Name}</Name>
 <BoundingSphereCenter x=""0"" y=""0"" z=""0"" />
 <BoundingSphereRadius value=""{F(g.Radius)}"" />
 <BoundingBoxMin {mn} />
 <BoundingBoxMax {mx} />
 <LodDistHigh value=""9998"" />
 <LodDistMed value=""9998"" />
 <LodDistLow value=""9998"" />
 <LodDistVlow value=""9998"" />
 <FlagsHigh value=""1"" />
 <FlagsMed value=""0"" />
 <FlagsLow value=""0"" />
 <FlagsVlow value=""0"" />
 <ShaderGroup>
  <Shaders>
   <Item>
    <Name>{m.Shader}</Name>
    <FileName>{m.Shader}.sps</FileName>
    <RenderBucket value=""{bucket}"" />
    <Parameters>
     <Item name=""DiffuseSampler"" type=""Texture"">
      <Name>{m.Texture}</Name>
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
    <Name>{m.Name}</Name>
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
     <BoundingBoxMin {mn} w=""0"" />
     <BoundingBoxMax {mx} w=""0"" />
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
{BoundsXml(m)} <Lights />
</Drawable>";
    }

    static string YtdXml(string srcDir, IEnumerable<string> textures)
    {
        var sb = new StringBuilder("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<TextureDictionary>\n");
        foreach (var t in textures)
        {
            var (w, h, mips) = DdsInfo(Path.Combine(srcDir, t + ".dds"));
            sb.Append($@" <Item>
  <Name>{t}</Name>
  <Unk32 value=""128"" />
  <Usage>DEFAULT</Usage>
  <UsageFlags>UNK24</UsageFlags>
  <ExtraFlags value=""0"" />
  <Width value=""{w}"" />
  <Height value=""{h}"" />
  <MipLevels value=""{mips}"" />
  <Format>D3DFMT_A8R8G8B8</Format>
  <FileName>{t}.dds</FileName>
 </Item>
");
        }
        sb.Append("</TextureDictionary>\n");
        return sb.ToString();
    }

    static string YtypXml(IEnumerable<(ModelDef m, Geo g)> models)
    {
        var sb = new StringBuilder();
        sb.Append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<CMapTypes>\n <extensions />\n <archetypes>\n");
        foreach (var (m, g) in models)
        {
            string phys = m.Half != null ? $"<physicsDictionary>{PropsRpf}</physicsDictionary>" : "<physicsDictionary />";
            sb.Append($@"  <Item type=""CBaseArchetypeDef"">
   <lodDist value=""300"" />
   <flags value=""537001984"" />
   <specialAttribute value=""0"" />
   <bbMin x=""{F(g.Min[0])}"" y=""{F(g.Min[1])}"" z=""{F(g.Min[2])}"" />
   <bbMax x=""{F(g.Max[0])}"" y=""{F(g.Max[1])}"" z=""{F(g.Max[2])}"" />
   <bsCentre x=""0"" y=""0"" z=""0"" />
   <bsRadius value=""{F(g.Radius)}"" />
   <hdTextureDist value=""100"" />
   <name>{m.Name}</name>
   <textureDictionary>{TexDict}</textureDictionary>
   <clipDictionary />
   <drawableDictionary />
   {phys}
   <assetType>ASSET_TYPE_DRAWABLE</assetType>
   <assetName>{m.Name}</assetName>
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
        // our archives are OPEN (unencrypted): GTA's keys are only needed to read the game's own files
        if (gameDir != "-") GTA5Keys.LoadFromPath(gameDir);
        var models = File.ReadAllLines(Path.Combine(srcDir, "models.txt"))
            .Where(l => l.Contains(';'))
            .Select(l => l.Split(';'))
            .Select(p => new ModelDef(p[0], p[1], p[2], int.Parse(p[3]), p[4] == "-" ? null : p[4].Split(',').Select(x => float.Parse(x, C)).ToArray()))
            .Select(m => (m, g: Geo.Load(Path.Combine(srcDir, m.Name + ".geo"))))
            .ToList();

        var files = new List<(string name, byte[] data)>();
        foreach (var (m, g) in models)
        {
            var doc = new XmlDocument();
            doc.LoadXml(DrawableXml(m, g));
            byte[] ydr = XmlMeta.GetData(doc, MetaFormat.Ydr, srcDir);
            if (ydr == null || ydr.Length < 16) throw new Exception($"ydr build failed for {m.Name}");
            files.Add(($"{m.Name}.ydr", ydr));
        }
        var textures = models.Select(x => x.m.Texture).Distinct().ToList();
        var ytdDoc = new XmlDocument();
        ytdDoc.LoadXml(YtdXml(srcDir, textures));
        byte[] ytd = XmlMeta.GetData(ytdDoc, MetaFormat.Ytd, srcDir);
        if (ytd == null || ytd.Length < 16) throw new Exception("ytd build failed");
        var ytypDoc = new XmlDocument();
        ytypDoc.LoadXml(YtypXml(models));
        byte[] ytyp = XmlMeta.GetData(ytypDoc, MetaFormat.RSC, srcDir);
        files.Add(($"{PropsRpf}.ytyp", ytyp));
        files.Add(($"{TexDict}.ytd", ytd)); // last: the ASI rewrites it with the player's own textures
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(outRpf))); // fresh checkouts have no build/
        WriteTexLayout(srcDir, ytd, Path.Combine(Path.GetDirectoryName(Path.GetFullPath(outRpf)), "dlc_tex.txt"));
        Console.WriteLine($"built {models.Count} drawables, {TexDict}.ytd ({textures.Count} textures, {ytd.Length / 1024} KB), {PropsRpf}.ytyp");

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
        CheckYtdIsLast(outRpf);
        return 0;
    }

    // Find each placeholder's marker in the decompressed texture dictionary: the ASI writes real pixels there.
    static void WriteTexLayout(string srcDir, byte[] ytd, string outPath)
    {
        uint sysFlags = BitConverter.ToUInt32(ytd, 8), gfxFlags = BitConverter.ToUInt32(ytd, 12);
        byte[] body;
        using (var ms = new MemoryStream(ytd, 16, ytd.Length - 16))
        using (var ds = new System.IO.Compression.DeflateStream(ms, System.IO.Compression.CompressionMode.Decompress))
        using (var outMs = new MemoryStream())
        {
            ds.CopyTo(outMs);
            body = outMs.ToArray();
        }
        var recipes = File.ReadAllLines(Path.Combine(srcDir, "tex_recipes.txt")).Where(l => l.Contains(';')).ToList();
        var sb = new StringBuilder();
        sb.Append("# GrandTheftMinecraft texture layout of gtm_tex.ytd (generated by gtmpack; no Minecraft content)\n");
        sb.Append($"ytd;{sysFlags};{gfxFlags};{body.Length}\n");
        for (int i = 0; i < recipes.Count; i++)
        {
            var p = recipes[i].Split(';');
            byte[] marker = Encoding.ASCII.GetBytes($"GTMTEX{i:D6}#");
            int off = IndexOf(body, marker);
            if (off < 0) throw new Exception($"marker for {p[0]} not found in the ytd");
            int w = int.Parse(p[1]), h = int.Parse(p[2]), size = 0, mips = 0;
            for (int mw = w, mh = h; ; mw = Math.Max(1, mw / 2), mh = Math.Max(1, mh / 2))
            {
                size += mw * mh * 4;
                mips++;
                if (mw == 1 && mh == 1) break;
            }
            if (off + size > body.Length) throw new Exception($"{p[0]}: pixel data runs past the ytd");
            sb.Append($"tex;{p[0]};{off};{size};{w};{h};{mips};{p[3]}\n");
        }
        File.WriteAllText(outPath, sb.ToString());
        Console.WriteLine($"wrote {outPath} ({recipes.Count} textures, ytd body {body.Length / 1024} KB)");
    }

    static int IndexOf(byte[] hay, byte[] needle)
    {
        for (int i = 0; i + needle.Length <= hay.Length; i++)
        {
            int k = 0;
            while (k < needle.Length && hay[i + k] == needle[k]) k++;
            if (k == needle.Length) return i;
        }
        return -1;
    }

    // The ASI grows gtm_tex.ytd in place, so it must be the last data in the props archive, and the props archive
    // the last data in dlc.rpf.
    static void CheckYtdIsLast(string rpfPath)
    {
        var rpf = new RpfFile(rpfPath, Path.GetFileName(rpfPath));
        rpf.ScanStructure(s => { }, e => Console.Error.WriteLine(e));
        var rootFiles = rpf.AllEntries.OfType<RpfFileEntry>().ToList();
        var props = rootFiles.First(e => e.NameLower == PropsRpf + ".rpf");
        if (rootFiles.Any(e => e != props && e.FileOffset > props.FileOffset))
            throw new Exception("props archive is not the last file in dlc.rpf");
        var child = rpf.Children.First(c => c.Name.Equals(PropsRpf + ".rpf", StringComparison.OrdinalIgnoreCase));
        var inner = child.AllEntries.OfType<RpfFileEntry>().ToList();
        var ytd = inner.First(e => e.NameLower == TexDict + ".ytd");
        if (inner.Any(e => e != ytd && e.FileOffset > ytd.FileOffset))
            throw new Exception("gtm_tex.ytd is not the last file in the props archive");
        Console.WriteLine("layout ok: gtm_tex.ytd is the last data in dlc.rpf");
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
        foreach (var name in new[] { "gtm_grass_block.ydr", "gtm_i_bow.ydr", TexDict + ".ytd", PropsRpf + ".ytyp", "content.xml" })
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
