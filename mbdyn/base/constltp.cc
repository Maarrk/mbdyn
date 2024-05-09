/* $Header$ */
/*
 * MBDyn (C) is a multibody analysis code.
 * http://www.mbdyn.org
 *
 * Copyright (C) 1996-2023
 *
 * Pierangelo Masarati	<pierangelo.masarati@polimi.it>
 * Paolo Mantegazza	<paolo.mantegazza@polimi.it>
 *
 * Dipartimento di Ingegneria Aerospaziale - Politecnico di Milano
 * via La Masa, 34 - 20156 Milano, Italy
 * http://www.aero.polimi.it
 *
 * Changing this copyright notice is forbidden.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation (version 2 of the License).
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "mbconfig.h"           /* This goes first in every *.c,*.cc file */

#include "myassert.h"
#include "mynewmem.h"
#include "constltp.h"
#include "mbpar.h"
#include "dataman.h"

namespace {
     class ConstLawReaderBase {
     protected:
          ConstLawReaderBase();
          virtual ~ConstLawReaderBase();
          virtual void Clear() = 0;
     public:
          static void ClearAll();
     private:
          ConstLawReaderBase* pNext;
          ConstLawReaderBase* pPrev;
          static ConstLawReaderBase* pHead;
     };

     template <typename Tstress, typename Tder, typename Tstrain = Tstress>
     class ConstLawReader: public ConstLawReaderBase {
     public:
          static bool SetCL(const std::string& name, std::unique_ptr<ConstitutiveLawRead<Tstress, Tder, Tstrain>>&& rf);
          static bool SetCL(const std::string& name, ConstitutiveLawRead<Tstress, Tder, Tstrain>* rf);
          static ConstitutiveLaw<Tstress, Tder, Tstrain>*
          ReadCL(const DataManager* pDM, MBDynParser& HP, ConstLawType::Type& CLType);
     private:
          virtual void Clear() override;

          typedef std::map<std::string, std::unique_ptr<ConstitutiveLawRead<Tstress, Tder, Tstrain>>, ltstrcase> CLFuncMapType;

          CLFuncMapType oFuncMap;
          static ConstLawReader oInstance;
     };

     ConstLawReaderBase* ConstLawReaderBase::pHead = nullptr;

     ConstLawReaderBase::ConstLawReaderBase()
     {
          pPrev = nullptr;
          pNext = pHead;

          if (pNext) {
               pNext->pPrev = this;
          }

          pHead = this;
     }

     ConstLawReaderBase::~ConstLawReaderBase()
     {
          if (pPrev) {
               ASSERT(pPrev->pNext == this);
               pPrev->pNext = pNext;
          }

          if (pNext) {
               ASSERT(pNext->pPrev == this);
               pNext->pPrev = pPrev;
          }

          if (this == pHead) {
               pHead = nullptr;
          }
     }

     void ConstLawReaderBase::ClearAll()
     {
          auto* pCurrent = pHead;

          ASSERT(pCurrent);
          
          while (pCurrent) {
               pCurrent->Clear();
               pCurrent = pCurrent->pNext;
          }
     }

     template <typename Tstress, typename Tder, typename Tstrain>
     bool ConstLawReader<Tstress, Tder, Tstrain>::SetCL(const std::string& name, std::unique_ptr<ConstitutiveLawRead<Tstress, Tder, Tstrain>>&& rf)
     {
          constexpr auto iDim = ConstLawHelper<Tstress>::iDim1;

          pedantic_cout("registering constitutive law " << iDim << "D \"" << name << "\"\n");

          const bool status =  oInstance.oFuncMap.insert(std::make_pair(name, std::move(rf))).second;

          ASSERT(status);

          return status;
     }

     template <typename Tstress, typename Tder, typename Tstrain>
     bool ConstLawReader<Tstress, Tder, Tstrain>::SetCL(const std::string& name, ConstitutiveLawRead<Tstress, Tder, Tstrain>* rf)
     {
          std::unique_ptr<ConstitutiveLawRead<Tstress, Tder, Tstrain>> ptr(rf);

          return SetCL(name, std::move(ptr));
     }

/* constitutive law parsing checkers */
     template <typename FuncMapType>
     class CLWordSetType : public HighParser::WordSet {
     public:
          explicit CLWordSetType(const FuncMapType& oFuncMap)
               :oFuncMap(oFuncMap) {
          }

          bool IsWord(const std::string& s) const {
               return oFuncMap.find(s) != oFuncMap.end();
          }
     private:
          const FuncMapType& oFuncMap;
     };

     template <typename Tstress, typename Tder, typename Tstrain>
     ConstitutiveLaw<Tstress, Tder, Tstrain>*
     ConstLawReader<Tstress, Tder, Tstrain>::ReadCL(const DataManager* pDM, MBDynParser& HP, ConstLawType::Type& CLType)
     {
          std::string s;
          const char* sOrig = HP.IsWord(CLWordSetType(oInstance.oFuncMap));

          if (sOrig == nullptr) {
               /* default to linear elastic? */
               s = "linear" "elastic";
               sOrig = "";
          } else {
               s = sOrig;
          }

          auto func = oInstance.oFuncMap.find(s);

          constexpr auto iDim = ConstLawHelper<Tstress>::iDim1;

          if (func == oInstance.oFuncMap.end()) {
               silent_cerr("unknown constitutive law " << iDim << "D type "
                           "\"" << sOrig << "\" "
                           "at line " << HP.GetLineData() << std::endl);
               throw DataManager::ErrGeneric(MBDYN_EXCEPT_ARGS);
          }

          return func->second->Read(pDM, HP, CLType);
     }

     template <typename Tstress, typename Tder, typename Tstrain>
     void ConstLawReader<Tstress, Tder, Tstrain>::Clear()
     {
          oFuncMap.clear();
     }

     template <typename Tstress, typename Tder, typename Tstrain>
     ConstLawReader<Tstress, Tder, Tstrain> ConstLawReader<Tstress, Tder, Tstrain>::oInstance;
}

/* functions that read a constitutive law */
ConstitutiveLaw<doublereal, doublereal> *
ReadCL1D(const DataManager* pDM, MBDynParser& HP, ConstLawType::Type& CLType) {
     return ConstLawReader<doublereal, doublereal>::ReadCL(pDM, HP, CLType);
}

ConstitutiveLaw<Vec3, Mat3x3> *
ReadCL3D(const DataManager* pDM, MBDynParser& HP, ConstLawType::Type& CLType) {
     return ConstLawReader<Vec3, Mat3x3>::ReadCL(pDM, HP, CLType);
}

ConstitutiveLaw<Vec6, Mat6x6> *
ReadCL6D(const DataManager* pDM, MBDynParser& HP, ConstLawType::Type& CLType) {
     return ConstLawReader<Vec6, Mat6x6>::ReadCL(pDM, HP, CLType);
}

ConstitutiveLaw<Vec7, Mat7x7> *
ReadCL7D(const DataManager* pDM, MBDynParser& HP, ConstLawType::Type& CLType) {
     return ConstLawReader<Vec7, Mat7x7>::ReadCL(pDM, HP, CLType);
}

ConstitutiveLaw<Vec9, Mat9x9>*
ReadCL9D(const DataManager* pDM, MBDynParser& HP, ConstLawType::Type& CLType) {
     return ConstLawReader<Vec9, Mat9x9>::ReadCL(pDM, HP, CLType);
}

/* constitutive law registration functions: call to register one */
bool
SetCL1D(const std::string& name, ConstitutiveLawRead<doublereal, doublereal>* rf) {
     return ConstLawReader<doublereal, doublereal>::SetCL(name, rf);
}

bool
SetCL3D(const std::string& name, ConstitutiveLawRead<Vec3, Mat3x3>* rf) {
     return ConstLawReader<Vec3, Mat3x3>::SetCL(name, rf);
}

bool
SetCL6D(const std::string& name, ConstitutiveLawRead<Vec6, Mat6x6>* rf) {
     return ConstLawReader<Vec6, Mat6x6>::SetCL(name, rf);
}

bool
SetCL7D(const std::string& name, ConstitutiveLawRead<Vec7, Mat7x7>* rf) {
     return ConstLawReader<Vec7, Mat7x7>::SetCL(name, rf);
}

bool
SetCL9D(const std::string& name, ConstitutiveLawRead<Vec9, Mat9x9>* rf) {
     return ConstLawReader<Vec9, Mat9x9>::SetCL(name, rf);     
}

void ClearCL() {
     ConstLawReaderBase::ClearAll();
}
