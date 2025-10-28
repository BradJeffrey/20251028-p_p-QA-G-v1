void make_mock_inputs(const char* outpath="20250928/data/smoke.root") {
  TFile f(outpath, "RECREATE");
  // INTT ADC: landau-like with tail
  TH1F hadc("h_InttRawHitQA_adc","",256,0,256);
  for (int i=0;i<50000;++i) hadc.Fill(gRandom->Landau(50,10));
  hadc.Write();
  // INTT phi ~ uniform
  TH1F hphi("h_InttClusterQA_clusterPhi_incl","",180,-3.14159,3.14159);
  for (int i=0;i<80000;++i) hphi.Fill(gRandom->Uniform(-3.14159,3.14159));
  hphi.Write();
  // BCO histogram with a peak
  TH1F hbco("h_InttRawHitQA_bco","",128,0,128);
  for (int i=0;i<40000;++i) hbco.Fill((int)gRandom->Gaus(32,4));
  hbco.Write();
  // INTT cluster size
  TH1F hcs("h_MvtxClusterQA_clusterSize","",10,0,10);
  for(int i=0;i<20000;++i) hcs.Fill(gRandom->Gaus(3,0.7));
  hcs.Write();
  // TPC phi size side0
  TH1F htpcphi0("h_TpcClusterQA_phisize_side0_0","",50,0,5);
  for(int i=0;i<10000;++i) htpcphi0.Fill(gRandom->Gaus(2.0,0.4));
  htpcphi0.Write();
  // TPC z error
  TH1F hzerr("h_TpcClusterQA_z_error_0","",50,0,2);
  for(int i=0;i<12000;++i) hzerr.Fill(gRandom->Gaus(0.6,0.15));
  hzerr.Write();
  f.Close();
}
