/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2011 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
 * Copyright (c) 2013 Budiarto Herman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Original work authors (from lte-enb-rrc.cc):
 * - Nicola Baldo <nbaldo@cttc.es>
 * - Marco Miozzo <mmiozzo@cttc.es>
 * - Manuel Requena <manuel.requena@cttc.es>
 *
 * Converted to handover algorithm interface by:
 * - Budiarto Herman <budiarto.herman@magister.fi>
 *
 * Modified by: Lucas Pacheco <lucassidpacheco@gmail.com>
 */

#include "multi-handover-algorithm.h"
#include <ns3/log.h>
#include <ns3/uinteger.h>
#include "ns3/core-module.h"
namespace ns3 {

NS_LOG_COMPONENT_DEFINE("MultiHandoverAlgorithm");

NS_OBJECT_ENSURE_REGISTERED(MultiHandoverAlgorithm);

///////////////////////////////////////////
// Handover Management SAP forwarder
///////////////////////////////////////////

MultiHandoverAlgorithm::MultiHandoverAlgorithm()
    : m_a2MeasId(0)
    , m_a4MeasId(0)
    , m_servingCellThreshold(30)
    , m_neighbourCellOffset(1)
    , m_handoverManagementSapUser(0)
{
    NS_LOG_FUNCTION(this);
    m_handoverManagementSapProvider = new MemberLteHandoverManagementSapProvider<MultiHandoverAlgorithm>(this);
}

MultiHandoverAlgorithm::~MultiHandoverAlgorithm()
{
    NS_LOG_FUNCTION(this);
}

TypeId
MultiHandoverAlgorithm::GetTypeId()
{
    static TypeId tid = TypeId("ns3::MultiHandoverAlgorithm")
                            .SetParent<LteHandoverAlgorithm>()
                            .SetGroupName("Lte")
                            .AddConstructor<MultiHandoverAlgorithm>()
                            .AddAttribute("ServingCellThreshold",
                                "If the RSRQ of the serving cell is worse than this "
                                "threshold, neighbour cells are consider for handover. "
                                "Expressed in quantized range of [0..34] as per Section "
                                "9.1.7 of 3GPP TS 36.133.",
                                UintegerValue(30),
                                MakeUintegerAccessor(&MultiHandoverAlgorithm::m_servingCellThreshold),
                                MakeUintegerChecker<uint8_t>(0, 34))
                            .AddAttribute("NeighbourCellOffset",
                                "Minimum offset between the serving and the best neighbour "
                                "cell to trigger the handover. Expressed in quantized "
                                "range of [0..34] as per Section 9.1.7 of 3GPP TS 36.133.",
                                UintegerValue(1),
                                MakeUintegerAccessor(&MultiHandoverAlgorithm::m_neighbourCellOffset),
                                MakeUintegerChecker<uint8_t>())
                            .AddAttribute("Threshold",
                                "lorem ipsum",
                                DoubleValue(0.2),
                                MakeDoubleAccessor(&MultiHandoverAlgorithm::m_threshold),
                                MakeDoubleChecker<double>())
                            .AddAttribute("TimeToTrigger",
                                "Time during which neighbour cell's RSRP "
                                "must continuously higher than serving cell's RSRP "
                                "in order to trigger a handover",
                                TimeValue(MilliSeconds(256)), // 3GPP time-to-trigger median value as per Section 6.3.5 of 3GPP TS 36.331
                                MakeTimeAccessor(&MultiHandoverAlgorithm::m_timeToTrigger),
                                MakeTimeChecker());
    return tid;
}

void MultiHandoverAlgorithm::SetLteHandoverManagementSapUser(LteHandoverManagementSapUser* s)
{
    NS_LOG_FUNCTION(this << s);
    m_handoverManagementSapUser = s;
}

LteHandoverManagementSapProvider*
MultiHandoverAlgorithm::GetLteHandoverManagementSapProvider()
{
    NS_LOG_FUNCTION(this);
    return m_handoverManagementSapProvider;
}

void MultiHandoverAlgorithm::DoInitialize()
{
    NS_LOG_FUNCTION(this);
    /*MEDIDAS BASEADAS EM EVENTO A4, OU SEJA
     *CÉLULA VIZINHA SE TORNA MELHOR QUE THRESHOLD.
     *
     *SE APLICA BEM A NOSSO ALGORITMO.
     */

    NS_LOG_LOGIC(this << " requesting Event A4 measurements"
                      << " (threshold=0)");

    LteRrcSap::ReportConfigEutra reportConfig;
    reportConfig.eventId = LteRrcSap::ReportConfigEutra::EVENT_A4;
    reportConfig.threshold1.choice = LteRrcSap::ThresholdEutra::THRESHOLD_RSRQ;
    reportConfig.threshold1.range = 0; // THRESHOLD BAIXO FACILITA DETECÇÃO
    reportConfig.triggerQuantity = LteRrcSap::ReportConfigEutra::RSRQ;
    reportConfig.timeToTrigger = m_timeToTrigger.GetMilliSeconds();
    reportConfig.reportInterval = LteRrcSap::ReportConfigEutra::MS480;
    m_a4MeasId = m_handoverManagementSapUser->AddUeMeasReportConfigForHandover(reportConfig);
    LteHandoverAlgorithm::DoInitialize();
}

void MultiHandoverAlgorithm::DoDispose()
{
    NS_LOG_FUNCTION(this);
    delete m_handoverManagementSapProvider;
}

void MultiHandoverAlgorithm::DoReportUeMeas(uint16_t rnti,
    LteRrcSap::MeasResults measResults)
{
    NS_LOG_FUNCTION(this << rnti << (uint16_t)measResults.measId);

    EvaluateHandover(rnti, measResults.rsrqResult, (uint16_t)measResults.measId);
    if (measResults.haveMeasResultNeighCells
        && !measResults.measResultListEutra.empty()) {
        for (std::list<LteRrcSap::MeasResultEutra>::iterator it = measResults.measResultListEutra.begin();
             it != measResults.measResultListEutra.end();
             ++it) {
            NS_ASSERT_MSG(it->haveRsrqResult == true,
                "RSRQ measurement is missing from cellId " << it->physCellId);
            UpdateNeighbourMeasurements(rnti, it->physCellId, it->rsrqResult);
        }
    }
    else {
        NS_LOG_WARN(this << " Event A4 received without measurement results from neighbouring cells");
    }

} // end of DoReportUeMeas

void MultiHandoverAlgorithm::EvaluateHandover(uint16_t rnti,
    uint8_t servingCellRsrq, uint16_t measId)
{
  if (Simulator::Now().GetSeconds() < 5)
    return;
    /*if (Simulator::Now().GetSeconds() < Seconds(StartTime)){
       std::cout << "skip\n";
       return;
    }
    if (Simulator::Now().GetSeconds() > Seconds(StopTime)){
       std::cout << "skip\n";
       return;
    }*/

    NS_LOG_FUNCTION(this << rnti << (uint16_t)servingCellRsrq);

    /*------------FIND MEASURES FOR GIVEN RNTI------------*/
    MeasurementTable_t::iterator it1;
    it1 = m_neighbourCellMeasures.find(rnti);

    if (it1 == m_neighbourCellMeasures.end()) {
        //NS_LOG_WARN("Skipping handover evaluation for RNTI " << rnti << " because neighbour cells information is not found");
    }
    else {
        MeasurementRow_t::iterator it2;
        double threshold = MultiHandoverAlgorithm::m_threshold;

        /*------------ASSOCIATE RNTI WITH CELLID------------*/
        //retrieve cell id from files
        std::stringstream rntiPath;
        rntiPath << "rnti/" << rnti << ".txt";

        std::ifstream servingCellId(rntiPath.str());

        /*if (servingCellId.fail()) {
            return;
        }*/

        int a, b; //aux variables
        while (servingCellId >> a >> b) {
        }

        /*-----------------DEFINE PARAMETERS-----------------*/
        uint16_t bestNeighbourCellId = b;
        //        uint8_t bestcell = 0;

        int i = 0;

        int n_p = 3; //número de parâmetros
        int n_c = it1->second.size() + 1; //número de células
        int counter = 0;

        // peso de cada um
        int parametros[n_p];
        parametros[0] = 2; // rsrq
        parametros[1] = 4; // qoe
        parametros[2] = 1; // qos

        float prior_sum; // soma das prioridades
        float eigenvector[n_p]; // autovetor
        float eigenvector_aux[n_p]; // autovetor

        //características das células
        double cell[it1->second.size() + 1][4];
        //        double soma_rsrq;
        double soma[n_c];
        double soma_res = 0;
        //        double res[n_c];

        double qosAtual = 0;
        double qoeAtual = 0;
        //CURRENT QOE VALUE

        std::stringstream qoeRnti;
        qoeRnti << "rnti/" << rnti << "-qoe.txt";
        std::ifstream qoeRntiFile;
        qoeRntiFile.open(qoeRnti.str());

        //current qos value
        std::stringstream qosRnti;
        qosRnti << "rnti/" << rnti << "-qos.txt";
        std::ifstream qosRntiFile;
        qosRntiFile.open(qosRnti.str());

        if (qoeRntiFile.is_open()) {
            while (qoeRntiFile >> qoeAtual) {
            }
            if(qoeAtual > 3)
              return;
        }


        if (qosRntiFile.is_open()) {
            while (qosRntiFile >> qosAtual) {
            }
        }
        /*----------------neighbor cell values----------------*/
        for (it2 = it1->second.begin(); it2 != it1->second.end(); ++it2) {
            std::stringstream qoeFileName;
            std::stringstream qosFileName;
            std::string qoeResult;
            std::string qosResult;

            qoeFileName << "qoeTorre" << (uint16_t)it2->first;
            qosFileName << "qosTorre" << (uint16_t)it2->first;

            std::ifstream qosFile(qosFileName.str());
            std::ifstream qoeFile(qoeFileName.str());

            cell[i][0] = (uint16_t)it2->second->m_rsrq;

            if (qoeFile.fail() || qoeFile.peek() == std::ifstream::traits_type::eof())
                cell[i][1] = 0; //prioritize cells not used
            else
                while (qoeFile >> qoeResult)
                    cell[i][1] = stod(qoeResult);

            if (qosFile.fail() || qosFile.peek() == std::ifstream::traits_type::eof())
                cell[i][2] = 0;
            else
                while (qosFile >> qosResult)
                    cell[i][2] = stod(qosResult);

            cell[i][3] = it2->first;

            ++i;
        }
        /*-----------------current cell values-----------------*/
        std::stringstream qoeFileName;
        std::stringstream qosFileName;
        std::string qoeResult;
        std::string qosResult;

        qoeFileName << "qoeTorre" << b;
        qosFileName << "qosTorre" << b;

        std::ifstream qosFile(qosFileName.str());
        std::ifstream qoeFile(qoeFileName.str());

        cell[i][0] = (uint16_t)servingCellRsrq;
        if (qoeAtual)
            cell[i][1] = qoeAtual;
        else {
            cell[i][1] = 0;
        }
        if(qosAtual)
            cell[i][2] = qosAtual;
        else
            cell[i][2] = 0;
        cell[i][3] = b;

        for (i = 0; i < n_c; ++i){
          soma[i] = cell[i][0] * 0.2 + cell[i][1] * 0.4 + 0.1 * cell[i][2];
          if (soma[i] > soma_res){
            soma_res = soma[i];
            bestNeighbourCellId = cell[i][3];
          }
        }

        /*-----------------------------EXECUÇÃO DO HANDOVER-----------------------------*/
        if (bestNeighbourCellId != 0 && bestNeighbourCellId != b && soma_res > 5) {
            m_handoverManagementSapUser->TriggerHandover(rnti, bestNeighbourCellId);
            for (int i = 0; i < n_c; ++i){
                if(cell[i][3] == b)
                  NS_LOG_INFO("\n\n\nCélula " << cell[i][3] <<" -- Soma Ahp:" << soma[i] << " (serving)");
                else
                  NS_LOG_INFO("\n\n\nCélula " << cell[i][3] <<" -- Soma Ahp:" << soma[i]);
                NS_LOG_INFO("         -- RSRQ: " << cell[i][0]);
                NS_LOG_INFO("         -- MOSp: " << cell[i][1]);
                NS_LOG_INFO("         -- PDR: " << cell[i][2]);
            }
            NS_LOG_INFO("Triggering Handover -- RNTI: " << rnti << " -- cellId:" << bestNeighbourCellId << "\n\n\n");
        }
    } // end of else of if (it1 == m_neighbourCellMeasures.end ())

} // end of EvaluateMeasurementReport

bool MultiHandoverAlgorithm::IsValidNeighbour(uint16_t cellId)
{
    NS_LOG_FUNCTION(this << cellId);

    /**
   * \todo In the future, this function can be expanded to validate whether the
   *       neighbour cell is a valid target cell, e.g., taking into account the
   *       NRT in ANR and whether it is a CSG cell with closed access.
   */

    return true;
}

void MultiHandoverAlgorithm::UpdateNeighbourMeasurements(uint16_t rnti,
    uint16_t cellId,
    uint8_t rsrq)
{
    NS_LOG_FUNCTION(this << rnti << cellId << (uint16_t)rsrq);
    MeasurementTable_t::iterator it1;
    it1 = m_neighbourCellMeasures.find(rnti);

    if (it1 == m_neighbourCellMeasures.end()) {
        // insert a new UE entry
        MeasurementRow_t row;
        std::pair<MeasurementTable_t::iterator, bool> ret;
        ret = m_neighbourCellMeasures.insert(std::pair<uint16_t, MeasurementRow_t>(rnti, row));
        NS_ASSERT(ret.second);
        it1 = ret.first;
    }

    NS_ASSERT(it1 != m_neighbourCellMeasures.end());
    Ptr<UeMeasure> neighbourCellMeasures;
    std::map<uint16_t, Ptr<UeMeasure> >::iterator it2;
    it2 = it1->second.find(cellId);

    if (it2 != it1->second.end()) {
        neighbourCellMeasures = it2->second;
        neighbourCellMeasures->m_cellId = cellId;
        neighbourCellMeasures->m_rsrp = 0;
        neighbourCellMeasures->m_rsrq = rsrq;
    }
    else {
        // insert a new cell entry
        neighbourCellMeasures = Create<UeMeasure>();
        neighbourCellMeasures->m_cellId = cellId;
        neighbourCellMeasures->m_rsrp = 0;
        neighbourCellMeasures->m_rsrq = rsrq;
        it1->second[cellId] = neighbourCellMeasures;
    }

} // end of UpdateNeighbourMeasurements

} // end of namespace ns3