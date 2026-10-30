// TLDCoop_DLC: the worn clothes. Two sided (both faces lit the right way round), alpha tested, a normal map made from
// the colour texture for the weave and seams, and ambient occlusion baked into the vertex colour (folds, the inside of
// a collar, under the arms).
Shader "TLDCoop/Cloth" {
    Properties {
        _Color ("Main colour", Color) = (1,1,1,1)
        _MainTex ("MainTex", 2D) = "white" {}
        _Normal ("Normal", 2D) = "bump" {}
        _NormalMapStrength ("Normal Map Strength", Range(0, 2)) = 0.8
        _Cutoff ("Alpha cutoff", Range(0, 1)) = 0.5
        _AO ("Baked occlusion", Range(0, 1)) = 1
        _Smoothness ("Smoothness", Range(0, 1)) = 0.12
        _OffsetFactor ("Depth offset factor (by layer)", Float) = 0
        _OffsetUnits ("Depth offset units (by layer)", Float) = 0
    }
    SubShader {
        Tags { "Queue" = "AlphaTest" "RenderType" = "TransparentCutout" }
        Cull Off
        Offset [_OffsetFactor], [_OffsetUnits]
        CGPROGRAM
        #pragma surface surf Standard alphatest:_Cutoff addshadow fullforwardshadows
        #pragma target 3.0
        sampler2D _MainTex;
        sampler2D _Normal;
        fixed4 _Color;
        half _NormalMapStrength, _AO, _Smoothness;
        struct Input { float2 uv_MainTex; float4 color : COLOR; float facing : VFACE; };
        void surf(Input IN, inout SurfaceOutputStandard o) {
            fixed4 c = tex2D(_MainTex, IN.uv_MainTex) * _Color;
            half ao = lerp(1, IN.color.r, _AO);
            o.Albedo = c.rgb * lerp(1, ao, 0.5);
            o.Occlusion = ao;
            half3 n = UnpackScaleNormal(tex2D(_Normal, IN.uv_MainTex), _NormalMapStrength);
            n.z *= IN.facing > 0 ? 1 : -1;  // the inside of a coat, lit as the inside
            o.Normal = n;
            o.Smoothness = _Smoothness;
            o.Metallic = 0;
            o.Alpha = c.a;
        }
        ENDCG
    }
    FallBack "Legacy Shaders/Transparent/Cutout/VertexLit"
}
